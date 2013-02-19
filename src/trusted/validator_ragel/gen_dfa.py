#!/usr/bin/python
# Copyright (c) 2012 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import copy
import itertools
import optparse
import re
import StringIO

import def_format


def Attribute(name):
  assert name in def_format.SUPPORTED_ATTRIBUTES
  return name


class Operand(object):

  __slots__ = [
      'read_write_attr',
      'arg_type',
      'size',
      'implicit',
      'index']

  read_write_attr_regex = r'[%s%s%s%s]?' % (
      def_format.OperandReadWriteMode.UNUSED,
      def_format.OperandReadWriteMode.READ,
      def_format.OperandReadWriteMode.WRITE,
      def_format.OperandReadWriteMode.READ_WRITE)
  arg_type_regex = r'[acdbfgioprtxBCDEGHIJLMNOPQRSUVWXY]'
  size_regex = (
      r'|2|7|b|d|do|dq|fq|o|p|pb|pd|pdw|pdwx|pdx|ph|phx|pi|pj|pjx|pk|pkx|'
      r'pq|pqw|pqwx|pqx|ps|psx|pw|q|r|s|sb|sd|se|si|sq|sr|ss|st|sw|sx|'
      r'v|w|x|y|z')
  implicit_mark_regex = r'\*?'

  operand_regex = re.compile(r'(%s)(%s)(%s)(%s)$' % (
      read_write_attr_regex,
      arg_type_regex,
      size_regex,
      implicit_mark_regex))

  @staticmethod
  def Parse(s, default_rw):
    m = Operand.operand_regex.match(s)

    return Operand(
        read_write_attr=m.group(1) or default_rw,
        arg_type=m.group(2),
        size=m.group(3),
        implicit=(m.group(4) == '*'))

  def __init__(self, read_write_attr, arg_type, size, implicit=False):
    self.read_write_attr = read_write_attr
    self.arg_type = arg_type
    self.size = size
    self.implicit = implicit

  def Readable(self):
    return self.read_write_attr in [def_format.OperandReadWriteMode.READ,
                                    def_format.OperandReadWriteMode.READ_WRITE]

  def Writable(self):
    return self.read_write_attr in [def_format.OperandReadWriteMode.WRITE,
                                    def_format.OperandReadWriteMode.READ_WRITE]

  def ResidesInModRM(self):
    return self.arg_type in [
        def_format.OperandType.CONTROL_REGISTER,
        def_format.OperandType.DEBUG_REGISTER,
        def_format.OperandType.REGISTER_IN_REG,
        def_format.OperandType.REGISTER_IN_RM,
        def_format.OperandType.REGISTER_OR_MEMORY,
        def_format.OperandType.MEMORY,
        def_format.OperandType.SEGMENT_REGISTER_IN_REG,
        def_format.OperandType.MMX_REGISTER_IN_RM,
        def_format.OperandType.MMX_REGISTER_IN_REG,
        def_format.OperandType.MMX_REGISTER_OR_MEMORY,
        def_format.OperandType.XMM_REGISTER_IN_RM,
        def_format.OperandType.XMM_REGISTER_IN_REG,
        def_format.OperandType.XMM_REGISTER_OR_MEMORY]

  def GetFormat(self):
    """Get human-readable string for operand type and size.

    This string is used as a suffix in action names like 'operand0_8bit'. Values
    set by these actions are in turn used by disassembler to determine how to
    print operand.

    Actually, there is one format ('memory') that is never returned because it
    is handled at a higher level.

    TODO(shcherbina): it is possible that format will also be needed by
    validator64 in order to identify zero-extending instruction, but I'm not
    sure how it will be done.

    Returns:
      String like '8bit', '32bit', 'xmm', 'mmx', etc.
    """
    if self.arg_type == def_format.OperandType.SEGMENT_REGISTER_IN_REG:
      return 'segreg'

    if self.arg_type in [def_format.OperandType.MMX_REGISTER_IN_REG,
                         def_format.OperandType.MMX_REGISTER_IN_RM]:
      assert self.size == 'q'
      return 'mmx'

    if self.size == 'b':
      return '8bit'
    if self.size == 'w':
      return '16bit'
    if self.size == 'd':
      return '32bit'
    if self.size == 'q':
      return '64bit'
    # TODO(shcherbina): support other formats.
    raise NotImplementedError()

  def __str__(self):
    return '%s%s%s%s' % (
        self.read_write_attr,
        self.arg_type,
        self.size,
        '*' if self.implicit else '')


class Instruction(object):

  __slots__ = [
      'name',
      'operands',
      'opcodes',
      'attributes',
      'rex',
      'required_prefixes',
      'optional_prefixes']

  class RexStatus(object):
    __slots__ = [
        'b_matters',
        'x_matters',
        'r_matters',
        'w_matters',
        'w_set']

  def __init__(self):
    self.opcodes = []
    self.attributes = []

    self.rex = self.RexStatus()
    self.rex.b_matters = False
    self.rex.x_matters = False
    self.rex.r_matters = False
    self.rex.w_matters = True
    self.rex.w_set = False

    self.required_prefixes = []
    self.optional_prefixes = []

  def CollectPrefixes(self):
    if Attribute('branch_hint') in self.attributes:
      self.optional_prefixes.append('branch_hint')
    if Attribute('condrep') in self.attributes:
      self.optional_prefixes.append('condrep')
    if Attribute('rep') in self.attributes:
      self.optional_prefixes.append('rep')
    if Attribute('lock') in self.attributes:
      self.optional_prefixes.append('lock')

    while True:
      opcode = self.opcodes[0]
      if opcode == 'rexw':
        self.rex.w_set = True
      elif opcode in [
          'data16',
          '0x66',  # data16 as well (the difference is that it is treated as
                   # part of opcode, and not as operand size indicator)
          '0xf0',  # lock
          '0xf2',  # rep(nz)
          '0xf3']:  # repz/condrep/branch_hint
        self.required_prefixes.append(opcode)
      else:
        # Prefixes ended, we get to the regular part of the opcode.
        break
      del self.opcodes[0]

  @staticmethod
  def Parse(line):
    """Parse one line of def file and return initialized Instruction object.

    Args:
      line: One line of def file (two or three columns separated by
          def_format.COLUMN_SEPARATOR).
          First column defines instruction name and operands,
          second one - opcodes and encoding details,
          third (optional) one - instruction attributes.

    Returns:
      Fully initialized Instruction object.
    """

    instruction = Instruction()

    columns = line.split(def_format.COLUMN_SEPARATOR)

    # Third column is optional.
    assert 2 <= len(columns) <= 3, line
    name_and_operands_column = columns[0]
    opcodes_column = columns[1]
    if len(columns) == 3:
      attributes = columns[2].split()
    else:
      attributes = []

    instruction.ParseNameAndOperands(name_and_operands_column)
    instruction.ParseOpcodes(opcodes_column)

    instruction.attributes = map(Attribute, attributes)

    return instruction

  def ParseNameAndOperands(self, s):
    # If instruction name is quoted, it is allowed to contain spaces.
    if s.startswith('"'):
      i = s.index('"', 1)
      self.name = s[1:i]
      operands = s[i+1:].split()
    else:
      operands = s.split()
      self.name = operands.pop(0)

    self.operands = []
    for i, op in enumerate(operands):
      # By default one- and two-operand instructions are assumed to read all
      # operands and store result to the last one, while instructions with
      # three or more operands are assumed to read all operands except last one
      # which is used to store the result of the execution.
      last = (i == len(operands) - 1)
      if last:
        if len(operands) <= 2:
          default_rw = def_format.OperandReadWriteMode.READ_WRITE
        else:
          default_rw = def_format.OperandReadWriteMode.WRITE
      else:
        default_rw = def_format.OperandReadWriteMode.READ
      operand = Operand.Parse(op, default_rw=default_rw)
      operand.index = i
      self.operands.append(operand)

  def ParseOpcodes(self, opcodes_column):
    opcodes = opcodes_column.split()

    # TODO(shcherbina): Just remove these 'opcodes' from .def files once
    # gen_dfa.py is enabled. We can't do that right now because old gen_dfa
    # uses them, even though they are redundant.
    opcodes = [opcode for opcode in opcodes if opcode not in ['/r', '/m', '/s']]

    opcode_regex = re.compile(
        r'0x[0-9a-f]{2}|'  # raw bytes
        r'data16|'
        r'rexw|'  # REX prefix with W-bit set
        r'/[0-7]|'  # opcode stored in ModRM byte
        r'/|'  # precedes 3DNow! opcode map
        r'RXB\.([01][0-9A-F]|[01]{5})|'  # VEX-specific, 5 bits (hex or binary)
        r'[xW01]\.(src|src1|dest|cntl|1111)\.[Lx01]\.[01]{2}'  # VEX-specific
    )

    assert len(opcodes) > 0
    for opcode in opcodes:
      assert opcode_regex.match(opcode), opcode

    self.opcodes = opcodes

  def HasModRM(self):
    return any(operand.ResidesInModRM() for operand in self.operands)

  def FindOperand(self, arg_type):
    result = None
    for operand in self.operands:
      if operand.arg_type == arg_type:
        assert result is None, 'multiple operands of type %s' % arg_type
        result = operand
    return result

  def HasRegisterInOpcode(self):
    return self.FindOperand(
        def_format.OperandType.REGISTER_IN_OPCODE) is not None

  def HasOpcodeInsteadOfImmediate(self):
    return '/' in self.opcodes

  def GetOpcodeInModRM(self):
    """Return either opcode (0-7) or None."""
    for opcode in self.opcodes:
      m = re.match(r'/(\d)$', opcode)
      if m is not None:
        return int(m.group(1))
    return None

  def GetMainOpcodePart(self):
    result = []
    for opcode in self.opcodes:
      if opcode.startswith('/'):
        # Either '/' (opcode in immediate) or '/0'-'/7' (opcode in ModRM).
        # Anyway, main part of the opcode is over.
        break
      result.append(opcode)
    return result

  def GetNameAsIdentifier(self):
    """Return name in a form suitable to use as part of C identifier.

    In principle, collisions are possible, but will result in compilation
    failure, so we are not checking for them here for simplicity.

    Returns:
      Instruction name with all non-alphanumeric characters replaced with '_'.
    """
    return re.sub(r'\W', '_', self.name)

  def IsVexOrXop(self):
    return (self.opcodes[0] == '0xc4' and self.name != 'les' or
            self.opcodes[0] == '0x8f' and self.name != 'pop')

  def __str__(self):
    result = ' '.join([self.name] + map(str, self.operands))
    result += ', ' + ' '.join(self.opcodes)
    if len(self.attributes) > 0:
      result += ', ' + ' '.join(self.attributes)
    return result.strip()

  def RMatters(self):
    """Return True iff rex.r bit influences instruction."""
    # TODO(shcherbina): perhaps do something with rex.r_matters field?

    if self.FindOperand(def_format.OperandType.REGISTER_IN_REG) is not None:
      return True
    if self.FindOperand(def_format.OperandType.XMM_REGISTER_IN_REG) is not None:
      return True

    # Control registers are switched from %cr0..%cr7 to %cr8..%cr15 iff rex.r
    # bit is set or instruction has 0xf0 (lock) prefix.
    # Hence, rex.r bit influences control register if and only if lock prefix
    # is not used.
    if self.FindOperand(def_format.OperandType.CONTROL_REGISTER) is not None:
      return '0xf0' not in self.required_prefixes

    return False


AddressMode = collections.namedtuple(
    'AddressMode',
    ['mode', 'x_matters', 'b_matters'])

ALL_ADDRESS_MODES = [
    AddressMode('operand_disp', False, True),
    AddressMode('operand_rip', False, False),
    AddressMode('single_register_memory', False, True),
    AddressMode('operand_sib_pure_index', True, False),
    AddressMode('operand_sib_base_index', True, True)]


DECODER = object()
VALIDATOR = object()


class InstructionPrinter(object):

  def __init__(self, mode, bitness):
    assert mode in [DECODER, VALIDATOR]
    assert bitness in [32, 64]
    self._mode = mode
    self._bitness = bitness
    self._out = StringIO.StringIO()

  def GetContent(self):
    return self._out.getvalue()

  def _PrintRexPrefix(self, instruction):
    """Print a machine for REX prefix."""
    if self._bitness != 64:
      return
    if instruction.IsVexOrXop():
      return
    if Attribute('norex') in instruction.attributes:
      return

    if instruction.rex.w_matters:
      if instruction.rex.w_set:
        self._out.write('REXW_RXB\n')
      else:
        self._out.write('REX_RXB?\n')
    else:
      self._out.write('REX_WRXB?\n')


  def _PrintOpcode(self, instruction):
    """Print a machine for opcode."""
    main_opcode_part = instruction.GetMainOpcodePart()
    if instruction.HasRegisterInOpcode():
      assert not instruction.HasModRM()
      assert not instruction.HasOpcodeInsteadOfImmediate()

      self._out.write(' '.join(main_opcode_part[:-1]))

      # Register is encoded in three least significant bits of the last byte
      # of the opcode (in 64-bit case REX.B bit also will be involved, but it
      # will be handled elsewhere).
      last_byte = int(main_opcode_part[-1], 16)
      assert last_byte & 0b00000111 == 0
      self._out.write(' (%s)' % '|'.join(
          hex(b) for b in range(last_byte, last_byte + 2**3)))

      # TODO(shcherbina): Print operand*_from_opcode(_x87) actions (or maybe
      # do it somewhere else).
    else:
      self._out.write(' '.join(main_opcode_part))

  def _PrintSpuriousRexInfo(self, instruction):
    if (self._mode == DECODER and
        self._bitness == 64 and
        not instruction.IsVexOrXop()):
      # Note that even if 'norex' attribute is present, we print
      # @spurious_rex_... actions because NOP needs them (and it has REX
      # prefix specified as part of the opcode).
      # TODO(shcherbina): fix that?
      if not instruction.rex.b_matters:
        self._out.write('@set_spurious_rex_b\n')
      if not instruction.rex.x_matters:
        self._out.write('@set_spurious_rex_x\n')
      if not instruction.rex.r_matters:
        self._out.write('@set_spurious_rex_r\n')
      if not instruction.rex.w_matters:
        self._out.write('@set_spurious_rex_w\n')

  def _PrintSignature(self, instruction):
    """Print actions specifying instruction name and info about its operands.

    Example signature:
      @instruction_add
      @operands_count_is_2
      @operand0_8bit
      @operand1_8bit

    Depending on the mode, parts of the signature may be omitted.

    Args:
      instruction: instruction (with full information about operand types, etc.)

    Returns:
      None.
    """
    if self._mode == DECODER:
      self._out.write('@instruction_%s\n' % instruction.GetNameAsIdentifier())
      self._out.write('@operands_count_is_%d\n' % len(instruction.operands))

    # TODO(shcherbina): 'memory' format?
    for operand in instruction.operands:
      if self._NeedOperandInfo(operand):
        self._out.write('@operand%d_%s\n' %
                        (operand.index, operand.GetFormat()))

    # TODO(shcherbina): print operand sources and extract implicit operands.

    # TODO(shcherbina): print '@last_byte_is_not_immediate' when approptiate.
    # TODO(shcherbina): print '@modifiable_instruction' in validator64 if
    # attribute 'nacl-amd64-modifiable' is present.

    for attr in instruction.attributes:
      if attr.startswith('CPUFeature_'):
        self._out.write('@%s\n' % attr)

    # TODO(shcherbina): att_show_name_suffix.

  def _NeedOperandInfo(self, operand):
    """Whether we need to print actions describing operand format and source."""
    if self._mode == VALIDATOR and self._bitness == 64:
      # TODO(shcherbina): In this case we are only interested in writable
      # regular registers.
      raise NotImplementedError()

    return self._mode == DECODER

  def _PrintOperandSource(self, operand, source):
    """Print action specifying operand source."""
    # TODO(shcherbina): add mechanism to check that all operand sources are
    # printed.
    if self._NeedOperandInfo(operand):
      self._out.write('@operand%d_%s\n' % (operand.index, source))

  def _PrintImplicitOperandSources(self, instruction):
    """Print actions specifying sources of implicit operands.

    Args:
      instruction: instruction.

    Returns:
      None.
    """
    for operand in instruction.operands:
      if operand.arg_type == def_format.OperandType.ACCUMULATOR:
        self._PrintOperandSource(operand, 'rax')
      elif operand.arg_type == def_format.OperandType.DS_SI:
        self._PrintOperandSource(operand, 'ds_rsi')
      elif operand.arg_type == def_format.OperandType.ES_DI:
        self._PrintOperandSource(operand, 'es_rdi')

    # TODO(shcherbina): handle other implicit operands.

  def _PrintLegacyPrefixes(self, instruction):
    """Print a machine for all combinations of legacy prefixes."""
    legacy_prefix_combinations = GenerateLegacyPrefixes(
        instruction.required_prefixes,
        instruction.optional_prefixes)
    assert len(legacy_prefix_combinations) > 0
    if legacy_prefix_combinations == [()]:
      return
    self._out.write('(%s)' % ' | '.join(
        ' '.join(combination)
        for combination in legacy_prefix_combinations
        if combination != ()))
    # Use '(...)?' since Ragel does not allow '( | ...)'.
    if () in legacy_prefix_combinations:
      self._out.write('?')
    self._out.write('\n')

  def _PrintImmediates(self, instruction):
    """Print a machine that parses immediate operands (if present)."""
    operand = instruction.FindOperand(def_format.OperandType.IMMEDIATE)
    if operand is not None:
      format = operand.GetFormat()
      if format == '2bit':
        assert False, 'not supported yet'
      elif format == '8bit':
        self._out.write('imm8\n')
      elif format == '16bit':
        self._out.write('imm16\n')
      elif format == '32bit':
        self._out.write('imm32\n')
      elif format == '64bit':
        self._out.write('imm64\n')
      else:
        assert False, format
      self._PrintOperandSource(operand, 'immediate')

    operand = instruction.FindOperand(def_format.OperandType.SECOND_IMMEDIATE)
    if operand is not None:
      format = operand.GetFormat()
      if format == '8bit':
        self._out.write('imm8n2\n')
      elif format == '16bit':
        self._out.write('imm16n2\n')
      else:
        assert False, format
      self._PrintOperandSource(operand, 'second_immediate')

  def PrintInstructionWithoutModRM(self, instruction):
    assert not instruction.IsVexOrXop(), 'not supported yet'
    assert not instruction.HasModRM()

    self._PrintLegacyPrefixes(instruction)
    self._PrintRexPrefix(instruction)

    assert instruction.GetOpcodeInModRM() is None

    assert not instruction.HasOpcodeInsteadOfImmediate(), 'should not happen'

    self._PrintOpcode(instruction)
    self._out.write('\n')

    self._PrintSignature(instruction)
    self._PrintSpuriousRexInfo(instruction)
    self._PrintImplicitOperandSources(instruction)

    self._PrintImmediates(instruction)

    # Displacement encoded in the instruction.
    operand = instruction.FindOperand(def_format.OperandType.ABSOLUTE_DISP)
    if operand is not None:
      self._PrintOperandSource(operand, 'absolute_disp')
      self._out.write('disp%d\n' % self._bitness)

    # Relative jump/call target encoded in the instruction.
    operand = instruction.FindOperand(def_format.OperandType.RELATIVE_TARGET)
    if operand is not None:
      format = operand.GetFormat()
      if format == '8bit':
        self._out.write('rel8\n')
      elif format == '16bit':
        self._out.write('rel16\n')
      elif format == '32bit':
        self._out.write('rel32\n')
      else:
        assert False, format
      self._PrintOperandSource(operand, 'jmp_to')

    # TODO(shcherbina): subtract NOP from XCHG

  def _PrintModRMOperandSources(self, instruction):
    """Print sources for operands encoded in modrm."""
    for operand in instruction.operands:
      if operand.arg_type in [
          def_format.OperandType.CONTROL_REGISTER,
          def_format.OperandType.DEBUG_REGISTER,
          def_format.OperandType.REGISTER_IN_REG,
          def_format.OperandType.XMM_REGISTER_IN_REG]:
        source = 'from_modrm_reg'
      elif operand.arg_type in [
          def_format.OperandType.MMX_REGISTER_IN_REG,
          def_format.OperandType.SEGMENT_REGISTER_IN_REG]:
        source = 'from_modrm_reg_norex'
      elif operand.arg_type in [
          def_format.OperandType.REGISTER_IN_RM,
          def_format.OperandType.XMM_REGISTER_IN_RM]:
        source = 'from_modrm_rm'
      elif operand.arg_type == def_format.OperandType.MMX_REGISTER_IN_RM:
        source = 'from_modrm_rm_norex'
      elif operand.arg_type == def_format.OperandType.MEMORY:
        source = 'rm'
      else:
        continue
      self._PrintOperandSource(operand, source)

  def PrintInstructionWithModRMReg(self, instruction):
    """Print instruction that encodes register in its ModRM.r/m field."""

    instruction = copy.deepcopy(instruction)

    assert not instruction.IsVexOrXop(), 'not supported yet'
    assert instruction.HasModRM()
    assert instruction.FindOperand(def_format.OperandType.MEMORY) is None

    if instruction.RMatters():
      instruction.rex.r_matters = True

    # TODO(shcherbina): generalize and move somewhere?
    if (instruction.FindOperand(
            def_format.OperandType.REGISTER_IN_RM) is not None or
        instruction.FindOperand(
            def_format.OperandType.XMM_REGISTER_IN_RM) is not None):
      instruction.rex.b_matters = True

    self._PrintLegacyPrefixes(instruction)
    self._PrintRexPrefix(instruction)

    self._PrintOpcode(instruction)
    self._out.write('\n')

    opcode_in_modrm = instruction.GetOpcodeInModRM()
    if opcode_in_modrm is None:
      if not instruction.HasOpcodeInsteadOfImmediate():
        self._PrintSignature(instruction)
        self._PrintImplicitOperandSources(instruction)

      if instruction.FindOperand(
          def_format.OperandType.SEGMENT_REGISTER_IN_REG) is None:
        self._out.write('modrm_registers\n')
      else:
        self._out.write('(modrm_registers & opcode_s)\n')
      self._PrintModRMOperandSources(instruction)

      if instruction.HasOpcodeInsteadOfImmediate():
        assert instruction.opcodes[-2] == '/'
        self._out.write('%s\n' % instruction.opcodes[-1])
        self._PrintSignature(instruction)
        self._PrintImplicitOperandSources(instruction)
    else:
      assert not instruction.HasOpcodeInsteadOfImmediate()
      assert instruction.FindOperand(
          def_format.OperandType.SEGMENT_REGISTER_IN_REG) is None
      self._out.write('(modrm_registers & opcode_%d)\n' % opcode_in_modrm)

      # We postpone printing signature until opcode in modrm is read.
      self._PrintSignature(instruction)
      self._PrintImplicitOperandSources(instruction)

      self._PrintModRMOperandSources(instruction)

    self._PrintSpuriousRexInfo(instruction)

    self._PrintImmediates(instruction)

  def PrintInstructionWithModRMMemory(self, instruction, address_mode):
    """Print instruction that has memory access.

    There are several addressing modes corresponding to various combinations of
    ModRM and SIB (and additional displacement fields).

    Args:
      instruction: instruction.
      address_mode: instance of AddressMode.

    Returns:
      None.
    """

    instruction = copy.deepcopy(instruction)

    assert not instruction.IsVexOrXop(), 'not supported yet'
    assert instruction.HasModRM()
    assert instruction.FindOperand(def_format.OperandType.MEMORY) is not None

    assert address_mode in ALL_ADDRESS_MODES

    if instruction.RMatters():
      instruction.rex.r_matters = True
    if address_mode.x_matters:
      instruction.rex.x_matters = True
    if address_mode.b_matters:
      instruction.rex.b_matters = True

    self._PrintLegacyPrefixes(instruction)
    self._PrintRexPrefix(instruction)

    self._PrintOpcode(instruction)
    self._out.write('\n')

    opcode_in_modrm = instruction.GetOpcodeInModRM()
    if opcode_in_modrm is None:
      if not instruction.HasOpcodeInsteadOfImmediate():
        self._PrintSignature(instruction)
        self._PrintImplicitOperandSources(instruction)

      # Here we print something like
      # (any @operand0_from_modrm_reg @operand1_rm any* &
      #  operand_sib_base_index)
      # The first term specifies operand sources (they are known after the first
      # byte we read, ModRM), in this case operand0 come from ModRM.reg, and
      # operand1 is memory operand.
      # The second term parses information about specific addressing mode
      # (together with disp), independently of which operand will refer to it.

      if instruction.FindOperand(
          def_format.OperandType.SEGMENT_REGISTER_IN_REG) is None:
        self._out.write('(any\n')
      else:
        self._out.write('(opcode_s\n')
      self._PrintModRMOperandSources(instruction)
      self._out.write('  any* &\n')

      self._out.write('%s)\n' % address_mode.mode)

      if instruction.HasOpcodeInsteadOfImmediate():
        assert instruction.opcodes[-2] == '/'
        self._out.write('%s\n' % instruction.opcodes[-1])

        self._PrintSignature(instruction)
        self._PrintImplicitOperandSources(instruction)
    else:
      # Here we print something like
      # (opcode_2 @operand1_rm any* &
      #  operand_sib_base_index)
      # Note that we postpone printing signature until we read opcode in modrm.
      assert not instruction.HasOpcodeInsteadOfImmediate()
      assert instruction.FindOperand(
          def_format.OperandType.SEGMENT_REGISTER_IN_REG) is None
      self._out.write('(opcode_%d\n' % opcode_in_modrm)
      self._PrintSignature(instruction)
      self._PrintImplicitOperandSources(instruction)
      self._PrintModRMOperandSources(instruction)
      self._out.write('  any* &\n')

      self._out.write('%s)\n' % address_mode.mode)

    # TODO(shcherbina): we don't have to parse parts like disp (only ModRM
    # (and possibly SIB)) to determine spurious rex bits. Perhaps move it
    # a little bit earlier? But it would require third intersectee, ew...
    self._PrintSpuriousRexInfo(instruction)

    # TODO(shcherbina): @check_access when appropriate.

    self._PrintImmediates(instruction)


def InstructionToString(mode, bitness, instruction):
  header = '# %s\n' % instruction

  if not instruction.HasModRM():
    printer = InstructionPrinter(mode, bitness)
    printer.PrintInstructionWithoutModRM(instruction)
    return header + printer.GetContent()

  if instruction.FindOperand(def_format.OperandType.MEMORY) is None:
    printer = InstructionPrinter(mode, bitness)
    printer.PrintInstructionWithModRMReg(instruction)
    return header + printer.GetContent()
  else:
    instrs = []
    for address_mode in ALL_ADDRESS_MODES:
      printer = InstructionPrinter(mode, bitness)
      printer.PrintInstructionWithModRMMemory(instruction, address_mode)
      instrs.append(printer.GetContent())
    return header + '(%s)\n' % '|\n'.join(instrs)


def SplitRM(instruction):
  """Split instruction into two versions (using register or memory).

  Args:
    instruction: instruction.

  Returns:
    List of one or two instructions. If original instruction contains operand
    that can be either register or memory (such as 'E'), two specific versions
    are produced. Otherwise, instruction is returned unchanged.
  """
  splits = {
      def_format.OperandType.REGISTER_OR_MEMORY: (
          def_format.OperandType.REGISTER_IN_RM,
          def_format.OperandType.MEMORY),
      def_format.OperandType.MMX_REGISTER_OR_MEMORY: (
          def_format.OperandType.MMX_REGISTER_IN_RM,
          def_format.OperandType.MEMORY),
      def_format.OperandType.XMM_REGISTER_OR_MEMORY: (
          def_format.OperandType.XMM_REGISTER_IN_RM,
          def_format.OperandType.MEMORY)}

  instr_register = copy.deepcopy(instruction)
  instr_memory = copy.deepcopy(instruction)
  splitted = False
  for i, operand in enumerate(instruction.operands):
    if operand.arg_type in splits:
      assert not splitted, 'more than one r/m-splittable operand'
      splitted = True
      (instr_register.operands[i].arg_type,
       instr_memory.operands[i].arg_type) = splits[operand.arg_type]

  if splitted:
    # Lock prefix is only allowed for instructions that access memory.
    assert 'lock' not in instruction.required_prefixes
    if 'lock' in instruction.optional_prefixes:
      instr_register.optional_prefixes.remove('lock')

    return [instr_register, instr_memory]
  else:
    return [instruction]


def SplitByteNonByte(instruction):
  """Split instruction into versions with byte-sized operands and larger ones.

  Args:
    instruction: instruction.

  Returns:
    List of one or two instructions. If original instruction contains operands
    with undetermined size, two more specific versions are produced, otherwise
    original instruction is returned unchanged.
  """

  instr_byte = copy.deepcopy(instruction)
  instr_nonbyte = copy.deepcopy(instruction)

  splitted = False
  for i, operand in enumerate(instruction.operands):
    if operand.size == '':
      splitted = True
      instr_byte.operands[i].size = 'b'
      if operand.arg_type == def_format.OperandType.IMMEDIATE:
        instr_nonbyte.operands[i].size = 'z'  # word/dword
      else:
        instr_nonbyte.operands[i].size = 'v'  # word/dword/qword

  if not splitted:
    return [instruction]

  # Set the last bit of the main part of the opcode if instruction uses
  # larger-than-byte operands.
  main_opcode_part = instruction.GetMainOpcodePart()
  last_byte = int(main_opcode_part[-1], 16)
  assert last_byte % 2 == 0
  instr_nonbyte.opcodes[len(main_opcode_part) - 1] = hex(last_byte | 1)

  # TODO(shcherbina): att-show-name-suffix-b

  return [instr_byte, instr_nonbyte]


def SplitVYZ(bitness, instruction):
  """Split instruction into versions with 16/32/64-bit operand sizes.

  Actual operand size is determined by the following table:
                            z     y     v
    rex.W=0, data16        16    32    16
    rex.W=0, no data16     32    32    32
    rex.W=1                32    64    64    (this row is only for 64-bit mode)

  When we only have a subset of 'z', 'y', 'v' operands and some rows are
  indistinguishable, we don't produce ones with redundant prefixes.

  Args:
    bitness: 32 or 64.
    instruction: instruction.

  Returns:
    List of one to three instructions with specific operand sizes.
  """
  assert bitness in [32, 64]

  instr16 = copy.deepcopy(instruction)
  instr32 = copy.deepcopy(instruction)
  instr64 = copy.deepcopy(instruction)

  z_present = False
  y_present = False
  v_present = False

  for i, operand in enumerate(instruction.operands):
    if operand.size == 'z':
      instr16.operands[i].size = 'w'
      instr32.operands[i].size = 'd'
      instr64.operands[i].size = 'd'
      z_present = True
    elif operand.size == 'y':
      instr16.operands[i].size = 'd'
      instr32.operands[i].size = 'd'
      instr64.operands[i].size = 'q'
      y_present = True
    elif operand.size == 'v':
      instr16.operands[i].size = 'w'
      instr32.operands[i].size = 'd'
      instr64.operands[i].size = 'q'
      v_present = True

  if not z_present and not y_present and not v_present:
    # Even when there are no operands to split by size, we usually want to
    # allow spurious rexw bit.
    # But we don't do that when instruction is split in def-file manually.
    if (Attribute('norex') in instruction.attributes or
        Attribute('norexw') in instruction.attributes or
        instruction.rex.w_set):
      return [instruction]

    # When data16 prefix is present, rexw definitely matters (because rexw takes
    # precedence over data16), so we don't mark it as irrelevant.
    # TODO(shcherbina): remove this exception if
    # https://code.google.com/p/nativeclient/issues/detail?id=3310 is fixed.
    if 'data16' in instruction.required_prefixes:
      return [instruction]

    instr = copy.deepcopy(instruction)
    instr.rex.w_matters = False
    return [instr]

  assert 'data16' not in instruction.required_prefixes
  instr16.required_prefixes.append('data16')
  instr16.rex.w_set = False
  instr32.rex.w_set = False
  instr64.rex.w_set = True

  result = []
  if z_present or v_present:
    result.append(instr16)
  result.append(instr32)
  if (bitness == 64 and
      (y_present or v_present) and
      Attribute('norexw') not in instruction.attributes):
    result.append(instr64)
  return result

  # TODO(shcherbina): att-show-name-suffix-...


def ParseDefFile(filename):
  # .def file format is documented in general_purpose_instructions.def.

  with open(filename) as def_file:
    lines = [line for line in def_file if not line.startswith('#')]

  lines = iter(lines)
  while True:
    line = next(lines, None)
    if line is None:
      break

    # Comma-terminated lines are expected to be continued.
    while line.endswith(',\n'):
      next_line = next(lines)
      line = line[:-2] + def_format.COLUMN_SEPARATOR + next_line

    assert '#' not in line

    line = line.strip()
    if line == '':
      continue

    yield Instruction.Parse(line)


def GenerateLegacyPrefixes(required_prefixes, optional_prefixes):
  """Produce list of all possible combinations of legacy prefixes.

  Legacy prefixes are defined in processor manual:
    operand-size override (data16),
    address-size override,
    segment override,
    LOCK,
    REP/REPE/REPZ,
    REPNE/REPNZ.

  All permutations are enumerated, but repeated prefixes are not allowed
  (they make state count too large), even though processor decoder allows
  repetitions.

  In the future we might want to decide on a single preferred order of prefixes
  that validator allows.

  Args:
    required_prefixes: List of prefixes that have to be included in each
        combination produced
    optional_prefixes: List of prefixes that may or may not be present in
        resulting combinations.
  Returns:
    List of tuples of prefixes.
  """
  all_prefixes = required_prefixes + optional_prefixes
  assert len(set(all_prefixes)) == len(all_prefixes), 'duplicate prefixes'

  required_prefixes = tuple(required_prefixes)

  result = []
  for k in range(len(optional_prefixes) + 1):
    for optional in itertools.combinations(optional_prefixes, k):
      for prefixes in itertools.permutations(required_prefixes + optional):
        result.append(prefixes)

  assert len(set(result)) == len(result), 'duplicate resulting combinations'

  return result


def main():
  parser = optparse.OptionParser(
      usage='%prog [options] def_files...')
  parser.add_option('--bitness',
                    type=int,
                    help='The subarchitecture: 32 or 64')
  parser.add_option('--mode',
                    help='validator or decoder')

  options, args = parser.parse_args()

  if options.bitness not in [32, 64]:
    parser.error('specify --bitness 32 or --bitness 64')

  if len(args) == 0:
    parser.error('specify at least one .def file')

  mode = {'validator': VALIDATOR, 'decoder': DECODER}[options.mode]

  print '/*'
  print ' * THIS FILE IS AUTO-GENERATED BY gen_dfa.py. DO NOT EDIT.'
  print ' * mode: %s' % options.mode
  print ' * bitness: %s' % options.bitness
  print ' */'
  print
  print '%%{'
  print '  machine decode_x86_%d;' % options.bitness
  print '  alphtype unsigned char;'

  printed_instrs = []

  instruction_names = set()

  for def_file in args:
    for instruction in ParseDefFile(def_file):
      instruction.CollectPrefixes()

      if options.bitness == 32 and Attribute('amd64') in instruction.attributes:
        continue
      if options.bitness == 64 and Attribute('ia32') in instruction.attributes:
        continue

      if mode == VALIDATOR:
        if Attribute('nacl-forbidden') in instruction.attributes:
          continue
        if (options.bitness == 32 and
            Attribute('nacl-ia32-forbidden') in instruction.attributes):
          continue
        if (options.bitness == 64 and
            Attribute('nacl-amd64-forbidden') in instruction.attributes):
          continue

      instruction_names.add((instruction.GetNameAsIdentifier(),
                             instruction.name))

      instructions = [instruction]
      instructions = sum(map(SplitRM, instructions), [])
      instructions = sum(map(SplitByteNonByte, instructions), [])
      instructions = sum([SplitVYZ(options.bitness, instr)
                          for instr in instructions],
                         [])

      header = '##### %s #####\n' % instruction
      variants = [InstructionToString(mode, options.bitness, instr)
                  for instr in instructions]
      printed_instrs.append(header + '|\n'.join(variants))

  for name_id, name in sorted(instruction_names):
    print '  action instruction_%s { SET_INSTRUCTION_NAME("%s"); }' % (
        name_id, name)

  print '  one_instruction = '
  print '|\n'.join(printed_instrs)

  print '  ;'
  print '}%%'


if __name__ == '__main__':
  main()
