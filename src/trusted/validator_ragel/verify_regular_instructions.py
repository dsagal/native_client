#!/usr/bin/python
# Copyright (c) 2013 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Generate all acceptable regular instructions by traversing validator DFA
and run objdump, new and old validator on them.
"""

import itertools
import multiprocessing
import optparse
import os
import re
import subprocess
import sys
import tempfile

import dfa_parser
import dfa_traversal
import validator


BUNDLE_SIZE = 32

FWAIT = 0x9b
NOP = 0x90


def IsRexPrefix(byte):
  return 0x40 <= byte < 0x50


class OldValidator(object):
  def __init__(self):
    self._bundles = []
    self._errors = []
    pass

  def Validate(self, bundle, comment):
    self._bundles.append((bundle, comment))

    if len(self._bundles) == 40:
      self._Process()

  def _Process(self):
    bytes = sum((instr for instr, _ in self._bundles), [])
    hex_content = ' '.join('%02x' % byte for byte in bytes).replace('0x', '')

    assert len(hex_content) < 4096

    ncval = {32: options.ncval32, 64: options.ncval64}[options.bitness]
    proc = subprocess.Popen(
        [ncval, '--hex_text=-', '--max_errors=-1'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE)

    stdout, stderr = proc.communicate(hex_content)
    return_code = proc.wait()
    assert return_code == 0, (stdout, stderr)

    if '*** <input> is safe ***' in stdout:
      self._bundles = []
      return

    assert '*** <input> IS UNSAFE ***' in stdout

    rejected_bundles = set()
    for line in stdout.split('\n'):
      line = line.strip()
      if line == '':
        continue
      if line == '*** <input> IS UNSAFE ***':
        continue
      if line == 'Some instructions were replaced with HLTs.':
        continue
      if line.startswith(
          'VALIDATOR: Checking block alignment and jump targets'):
        continue
      m = re.match(r'VALIDATOR: ([0-9a-f]+): (.*)$', line, re.IGNORECASE)
      assert m is not None, (line, hex_content)
      error_offset = int(m.group(1), 16)
      rejected_bundles.add(error_offset // BUNDLE_SIZE)

    assert len(rejected_bundles) != 0
    for b in sorted(rejected_bundles):
      _, comment = self._bundles[b]
      self._errors.append(comment)

    self._bundles = []

  def GetErrors(self):
    if len(self._bundles) > 0:
      self._Process()
    return self._errors


def ValidateInstruction(instruction, disassembly, old_validator):
  assert len(instruction) <= BUNDLE_SIZE
  bundle = instruction + [NOP] * (BUNDLE_SIZE - len(instruction))

  if options.bitness == 32:
    result = validator.ValidateChunk(
        ''.join(map(chr, bundle)),
        bitness=options.bitness)

    if result:
      old_validator.Validate(bundle, (disassembly, instruction))
    return result
  else:
    # TODO(shcherbina): implement.
    return False


class WorkerState(object):
  def __init__(self, prefix):
    self.total_instructions = 0
    self.num_valid = 0
    self._file_prefix = 'check_validator_%s_' % '_'.join(map(hex, prefix))
    self._instructions = []
    self.errors = []

  def ReceiveInstruction(self, bytes):
    self._instructions.append(bytes)

    # Objdump prints crazy stuff when x87 instructions are prefixed with
    # fwait (especially when REX prefixes are involved). To avoid that,
    # we insert nops after each fwait.
    if (bytes == [FWAIT] or
        len(bytes) == 2 and IsRexPrefix(bytes[0]) and bytes[1] == FWAIT):
      self._instructions.append([NOP])

    if len(self._instructions) >= 1000000:
      self.Finish()
      self._instructions = []

  def Finish(self):
    # Check instructions accumulated so far.
    try:
      raw_file = tempfile.NamedTemporaryFile(
          mode='wb',
          prefix=self._file_prefix,
          suffix='.o',
          delete=False)
      for instr in self._instructions:
        raw_file.write(''.join(map(chr, instr)))
      raw_file.close()

      objdump_proc = subprocess.Popen(
          [options.objdump,
           '-D',
           '-b', 'binary',
           '-m', 'i386'] +
          {32: [], 64: ['-M', 'x86-64']}[options.bitness] +
          ['--insn-width', '15',
           raw_file.name],
          stdout=subprocess.PIPE)

      # Objdump prints few lines about file and section before disassembly
      # listing starts, so we have to skip that.
      objdump_header_size = 7

      objdump_iter = iter(objdump_proc.stdout)
      for i in range(objdump_header_size):
        next(objdump_iter)

      old_validator = OldValidator()
      for instr in self._instructions:
        # Objdump prints fwait with REX prefix in this ridiculous way:
        #   0: 41    fwait
        #   1: 9b    fwait
        # So in such cases we expect two lines from objdump.
        if len(instr) == 2 and IsRexPrefix(instr[0]) and instr[1] == FWAIT:
          expected_lines = 2
        else:
          expected_lines = 1

        bytes = []
        for _ in range(expected_lines):
          line = next(objdump_iter)
          # Parse tab-separated line of the form
          # 0:  f2 40 0f 10 00        rex movsd (%rax),%xmm0
          addr, more_bytes, disassembly = line.strip().split('\t')
          more_bytes = [int(b, 16) for b in more_bytes.split()]
          bytes += more_bytes

        assert bytes == instr, (map(hex, bytes), map(hex, instr))
        self.total_instructions += 1

        self.num_valid += ValidateInstruction(instr, disassembly, old_validator)

      # Make sure we read objdump output to the end.
      end = next(objdump_iter, None)
      assert end is None, end

      return_code = objdump_proc.wait()
      assert return_code == 0

    finally:
      os.remove(raw_file.name)

    errors = old_validator.GetErrors()
    for error in errors:
      print error
    self.errors += errors


def Worker((prefix, state_index)):
  worker_state = WorkerState(prefix)

  try:
    dfa_traversal.TraverseTree(
        states[state_index],
        final_callback=worker_state.ReceiveInstruction,
        prefix=prefix)
  finally:
    worker_state.Finish()

  return (
      prefix,
      worker_state.total_instructions,
      worker_state.num_valid,
      worker_state.errors)


def ParseOptions():
  parser = optparse.OptionParser(usage='%prog [options] xmlfile')

  parser.add_option('--bitness',
                    type=int,
                    help='The subarchitecture: 32 or 64')
  parser.add_option('--gas',
                    help='Path to GNU AS executable')
  parser.add_option('--objdump',
                    help='Path to objdump executable')
  parser.add_option('--validator_dll',
                    help='Path to librdfa_validator_dll')
  parser.add_option('--ncval32',
                    help='Path to old 32-bit ncval')
  parser.add_option('--ncval64',
                    help='Path to old 64-bit ncval')
  parser.add_option('--errors',
                    help='Where to save errors')

  options, args = parser.parse_args()

  if options.bitness not in [32, 64]:
    parser.error('specify -b 32 or -b 64')

  if not (options.gas and options.objdump and options.validator_dll):
    parser.error('specify path to gas, objdump, and validator_dll')

  if not (options.ncval32 and options.ncval64):
    parser.error('specify path to old validator (32-bit and 64-bit versions)')

  if not options.errors:
    parser.errors('specify file to save errors to')

  if not os.path.exists(options.ncval32):
    print options.ncval32, 'not found (try ./scons ncval platform=x86-32)'
    sys.exit(1)
  if not os.path.exists(options.ncval64):
    print options.ncval64, 'not found (try ./scons ncval platform=x86-64)'
    sys.exit(1)

  if len(args) != 1:
    parser.error('specify one xml file')

  (xml_file,) = args

  return options, xml_file


options, xml_file = ParseOptions()
# We are doing it here to share state graph between workers spawned by
# multiprocess. Passing it every time is slow.
states, initial_state = dfa_parser.ParseXml(xml_file)

validator.Init(options.validator_dll)


def main():
  assert initial_state.is_accepting
  assert not initial_state.any_byte

  print len(states), 'states'

  num_suffixes = dfa_traversal.GetNumSuffixes(initial_state)

  # We can't just write 'num_suffixes[initial_state]' because
  # initial state is accepting.
  total_instructions = sum(num_suffixes[t.to_state]
                           for t in initial_state.forward_transitions.values())
  print total_instructions, 'regular instructions total'

  tasks = dfa_traversal.CreateTraversalTasks(states, initial_state)
  print len(tasks), 'tasks'

  pool = multiprocessing.Pool()

  results = pool.imap(Worker, tasks)

  total = 0
  num_valid = 0
  errors = []
  for prefix, count, valid_count, more_errors in results:
    print ', '.join(map(hex, prefix))
    total += count
    num_valid += valid_count
    errors += more_errors

  print total, 'instructions were processed'
  print num_valid, 'valid instructions'

  print len(errors), 'errors'

  errors.sort()
  with open(options.errors, 'w') as errors_file:
    errors_file.write(
        'Instructions accepted by new validator but rejected by old one:\n')
    for disassembly, bytes in errors:
      hex_bytes = ' '.join('%02x' % byte for byte in bytes).replace('0x', '')
      errors_file.write('%-50s %s\n' % (disassembly, hex_bytes))


if __name__ == '__main__':
  main()
