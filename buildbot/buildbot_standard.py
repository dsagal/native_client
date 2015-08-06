#!/usr/bin/python
# Copyright (c) 2012 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Enable 'with' statements in Python 2.5
from __future__ import with_statement

import os.path
import platform
import re
import shutil
import subprocess
import sys
import time

from buildbot_lib import (
    BuildContext, BuildStatus, Command, EnsureDirectoryExists,
    ParseStandardCommandLine, RemoveDirectory, RemovePath,
    RemoveGypBuildDirectories, RemoveSconsBuildDirectories, RunBuild, SCons,
    SetupLinuxEnvironment, SetupMacEnvironment, SetupWindowsEnvironment,
    SetupAndroidEnvironment, Step, StepLink, StepText, TryToCleanContents,
    RunningOnBuildbot)


def SetupContextVars(context):
  # The branch is set to native_client on the main bots, on the trybots it's
  # set to ''.  Otherwise, we should assume a particular branch is being used.
  context['branch'] = os.environ.get('BUILDBOT_BRANCH', 'native_client')
  context['off_trunk'] = context['branch'] not in ['native_client', '']


def ValidatorTest(context, architecture, validator, warn_only=False):
  cmd = [
      sys.executable,
      'tests/abi_corpus/validator_regression_test.py',
      '--keep-going',
      '--validator', validator,
      '--arch', architecture
  ]
  if warn_only:
    cmd.append('--warn-only')
  Command(context, cmd=cmd)


def SummarizeCoverage(context):
  Command(context, [
      sys.executable,
      'tools/coverage_summary.py',
      context['platform'] + '-' + context['default_scons_platform'],
  ])


def ArchiveCoverage(context):
  gsutil = '/b/build/third_party/gsutil/gsutil'
  gsd_url = 'http://gsdview.appspot.com/nativeclient-coverage2/revs'
  variant_name = ('coverage-' + context['platform'] + '-' +
                  context['default_scons_platform'])
  coverage_path = variant_name + '/html/index.html'
  revision = os.environ.get('BUILDBOT_REVISION', 'None')
  link_url = gsd_url + '/' + revision + '/' + coverage_path
  gsd_base = 'gs://nativeclient-coverage2/revs'
  gs_path = gsd_base + '/' + revision + '/' + variant_name
  cov_dir = 'scons-out/' + variant_name + '/coverage'
  # Copy lcov file.
  Command(context, [
      sys.executable, gsutil,
      'cp', '-a', 'public-read',
      cov_dir + '/coverage.lcov',
      gs_path + '/coverage.lcov',
  ])
  # Copy html.
  Command(context, [
      sys.executable, gsutil,
      'cp', '-R', '-a', 'public-read',
      'html', gs_path,
  ], cwd=cov_dir)
  print '@@@STEP_LINK@view@%s@@@' % link_url


def CommandGypBuild(context):
  # Do not use goma when inside a toolchain build, because the
  # freshly-built NaCl compilers will never be available via goma.
  # This sacrifices the benefits of goma for building the trusted
  # code too, but it's not clear how to teach Gyp to use goma for
  # some compilers and not others.
  use_goma = (RunningOnBuildbot() and
              not context['no_goma'] and
              not context['inside_toolchain'])

  if use_goma:
    # Since this is for buildbot, it should not be good to use the result
    # generated by the different version compiler.
    os.environ['GOMA_HERMETIC'] = 'fallback'

  runtest_py = os.environ.get('RUNTEST')
  alt_runtest_py = '/b/build/scripts/slave/runtest.py'
  if runtest_py is None and os.path.exists(alt_runtest_py):
    runtest_py = alt_runtest_py

  # TODO(bradnelson): Figure out why win64 trybots can't upload goma logs.
  buildername = os.environ.get('BUILDBOT_BUILDERNAME', '')
  excluded_os = False
  for os in ['win64', 'vista', 'win7-64', 'win8-64']:
    if os in buildername:
      excluded_os = True

  if runtest_py is None or excluded_os:
    # Fallback to direct goma + ninja if not run on bots.
    try:
      if use_goma:
        Command(context, cmd=[
            sys.executable, '/b/build/goma/goma_ctl.py', 'restart'])
      cmd = ['ninja', '-v', '-k', '0', '-C', '../out/' + context['gyp_mode']]
      if use_goma:
        cmd += ['-j50']
      Command(context, cmd=cmd)
    finally:
      if use_goma:
        Command(context, cmd=[
            sys.executable, '/b/build/goma/goma_ctl.py', 'stop'])
  else:
    # Infer the location of compile.py from runtest.py.
    compile_py = os.path.join(os.path.dirname(runtest_py), 'compile.py')
    cmd = [sys.executable, compile_py, '--target', context['gyp_mode'],
           '--src-dir', '../', '--build-tool', 'ninja',
           '--ninja-ensure-up-to-date']
    if use_goma:
      cmd += ['--compiler', 'goma']
      cmd += ['--goma-dir', '/b/build/goma']
    # Verbose and but stop on fail.
    cmd += ['--', '-v', '-k', '0']
    Command(context, cmd=cmd)


def CommandGypGenerate(context):
  Command(
          context,
          cmd=[sys.executable, 'native_client/build/gyp_nacl'],
          cwd='..')


def CommandGclientRunhooks(context):
  if context.Windows():
    gclient = 'gclient.bat'
  else:
    gclient = 'gclient'
  print 'Running gclient runhooks...'
  print 'GYP_CROSSCOMPILE=' + context.GetEnv('GYP_CROSSCOMPILE', '')
  print 'GYP_GENERATORS=' + context.GetEnv('GYP_GENERATORS', '')
  print 'GYP_MSVS_VERSION=' + context.GetEnv('GYP_MSVS_VERSION', '')
  print 'GYP_DEFINES=' + context.GetEnv('GYP_DEFINES', '')
  Command(context, cmd=[gclient, 'runhooks', '--force'])


def BuildScript(status, context):
  inside_toolchain = context['inside_toolchain']

  # Clean out build directories.
  with Step('clobber', status):
    RemoveSconsBuildDirectories()
    RemoveGypBuildDirectories()

  with Step('cleanup_temp', status):
    # Picking out drive letter on which the build is happening so we can use
    # it for the temp directory.
    if context.Windows():
      build_drive = os.path.splitdrive(os.path.abspath(__file__))[0]
      tmp_dir = os.path.join(build_drive, os.path.sep + 'temp')
      context.SetEnv('TEMP', tmp_dir)
      context.SetEnv('TMP', tmp_dir)
    else:
      tmp_dir = '/tmp'
    print 'Making sure %s exists...' % tmp_dir
    EnsureDirectoryExists(tmp_dir)
    print 'Cleaning up the contents of %s...' % tmp_dir
    # Only delete files and directories like:
    #   */nacl_tmp/*
    # TODO(bradnelson): Drop this after a bit.
    # Also drop files and directories like these to cleanup current state:
    #   */nacl_tmp*
    #   */nacl*
    #   83C4.tmp
    #   .org.chromium.Chromium.EQrEzl
    #   tmp_platform*
    #   tmp_mmap*
    #   tmp_pwrite*
    #   tmp_syscalls*
    #   workdir*
    #   nacl_chrome_download_*
    #   browserprofile_*
    #   tmp*
    file_name_re = re.compile(
        r'[\\/\A]('
        r'tmp_nacl[\\/].+|'
        r'tmp_nacl.+|'
        r'nacl.+|'
        r'[0-9a-fA-F]+\.tmp|'
        r'\.org\.chrom\w+\.Chrom\w+\.[^\\/]+|'
        r'tmp_platform[^\\/]+|'
        r'tmp_mmap[^\\/]+|'
        r'tmp_pwrite[^\\/]+|'
        r'tmp_syscalls[^\\/]+|'
        r'workdir[^\\/]+|'
        r'nacl_chrome_download_[^\\/]+|'
        r'browserprofile_[^\\/]+|'
        r'tmp[^\\/]+'
        r')$')
    file_name_filter = lambda fn: file_name_re.search(fn) is not None

    # Clean nacl_tmp/* separately, so we get a list of leaks.
    nacl_tmp = os.path.join(tmp_dir, 'nacl_tmp')
    if os.path.exists(nacl_tmp):
      for bot in os.listdir(nacl_tmp):
        bot_path = os.path.join(nacl_tmp, bot)
        print 'Cleaning prior build temp dir: %s' % bot_path
        sys.stdout.flush()
        if os.path.isdir(bot_path):
          for d in os.listdir(bot_path):
            path = os.path.join(bot_path, d)
            print 'Removing leftover: %s' % path
            sys.stdout.flush()
            RemovePath(path)
          os.rmdir(bot_path)
        else:
          print 'Removing rogue file: %s' % bot_path
          RemovePath(bot_path)
      os.rmdir(nacl_tmp)
    # Clean /tmp so we get a list of what's accumulating.
    TryToCleanContents(tmp_dir, file_name_filter)

    # Recreate TEMP, as it may have been clobbered.
    if 'TEMP' in os.environ and not os.path.exists(os.environ['TEMP']):
      os.makedirs(os.environ['TEMP'])

    # Mac has an additional temporary directory; clean it up.
    # TODO(bradnelson): Fix Mac Chromium so that these temp files are created
    #     with open() + unlink() so that they will not get left behind.
    if context.Mac():
      subprocess.call(
          "find /var/folders -name '.org.chromium.*' -exec rm -rfv '{}' ';'",
          shell=True)
      subprocess.call(
          "find /var/folders -name '.com.google.Chrome*' -exec rm -rfv '{}' ';'",
          shell=True)

  # Skip over hooks when run inside the toolchain build because
  # package_version would overwrite the toolchain build.
  if inside_toolchain:
    with Step('gyp_generate_only', status):
      CommandGypGenerate(context)
  else:
    with Step('gclient_runhooks', status):
      CommandGclientRunhooks(context)

  # Make sure our GN build is working.
  can_use_gn = context.Linux() and context['arch'] != 'arm'
  gn_out = '../out'

  if can_use_gn:
    def BoolFlag(cond):
      return 'true' if cond else 'false'

    gn_x86 = 'false'
    gn_x64 = 'false'
    gn_arm = 'false'

    if context['arch'] == '32':
      gn_x86 = 'true'
    elif context['arch'] == '64':
      gn_x64 = 'true'
    elif context['arch'] == 'arm':
      gn_arm = 'true'
    else:
      raise Exception("Unexpected arch: " + context['arch'])

    gn_newlib = BoolFlag(not context['use_glibc'])
    gn_glibc = BoolFlag(context['use_glibc'])

    gn_gen_args = [
      'is_debug=' + context['gn_is_debug'],
      'use_trusted_x86=' + gn_x86,
      'use_nacl_x86=' + gn_x86,
      'use_trusted_x64=' + gn_x64,
      'use_nacl_x64=' + gn_x64,
      'use_trusted_arm=' + gn_arm,
      'use_nacl_arm=' + gn_arm,
      'use_gcc_newlib=' + gn_newlib,
      'use_gcc_glibc=' + gn_glibc,
      'use_clang_newlib=' + gn_newlib,
    ]

    # If this is a 32-bit build but the kernel reports as 64-bit,
    # then gn will set host_cpu=x64 when we want host_cpu=x86.
    if context['arch'] == '32':
      gn_gen_args.append('host_cpu="x86"')

    gn_cmd = [
      'gn',
      '--dotfile=../native_client/.gn', '--root=..',
      # Note: quotes are not needed around this space-separated
      # list of args.  The shell would remove them before passing
      # them to a program, and Python bypasses the shell.  Adding
      # quotes will cause an error because GN will see unexpected
      # double quotes.
      '--args=%s' % ' '.join(gn_gen_args),
      'gen', gn_out,
      ]

    with Step('gn_compile', status):
      Command(context, cmd=gn_cmd)
      Command(context, cmd=['ninja', '-C', gn_out])

  if context['clang']:
    with Step('update_clang', status):
      Command(context, cmd=['../tools/clang/scripts/update.sh'])

  # Just build both bitages of validator and test for --validator mode.
  if context['validator']:
    with Step('build ragel_validator-32', status):
      SCons(context, platform='x86-32', parallel=True, args=['ncval_new'])
    with Step('build ragel_validator-64', status):
      SCons(context, platform='x86-64', parallel=True, args=['ncval_new'])

    # Check validator trie proofs on both 32 + 64 bits.
    with Step('check validator proofs', status):
      SCons(context, platform='x86-64', parallel=False, args=['dfachecktries'])

    with Step('predownload validator corpus', status):
      Command(context,
          cmd=[sys.executable,
               'tests/abi_corpus/validator_regression_test.py',
               '--download-only'])

    with Step('validator_regression_test ragel x86-32', status,
        halt_on_fail=False):
      ValidatorTest(
          context, 'x86-32',
          'scons-out/opt-linux-x86-32/staging/ncval_new')
    with Step('validator_regression_test ragel x86-64', status,
        halt_on_fail=False):
      ValidatorTest(
          context, 'x86-64',
          'scons-out/opt-linux-x86-64/staging/ncval_new')

    return

  # Run checkdeps script to vet #includes.
  with Step('checkdeps', status):
    Command(context, cmd=[sys.executable, 'tools/checkdeps/checkdeps.py'])

  # Make sure our Gyp build is working.
  if not context['no_gyp']:
    with Step('gyp_compile', status):
      CommandGypBuild(context)

  # On a subset of Linux builds, build Breakpad tools for testing.
  if context['use_breakpad_tools']:
    with Step('breakpad configure', status):
      Command(context, cmd=['mkdir', '-p', 'breakpad-out'])
      Command(context, cwd='breakpad-out',
              cmd=['bash', '../../breakpad/configure',
                   'CXXFLAGS=-I../..'])  # For third_party/lss
    with Step('breakpad make', status):
      Command(context, cmd=['make', '-j%d' % context['max_jobs'],
                            # This avoids a broken dependency on
                            # src/third_party/lss files within the breakpad
                            # source directory.  We are not putting lss
                            # there, but using the -I switch above to
                            # find the lss in ../third_party instead.
                            'includelss_HEADERS=',
                            ],
              cwd='breakpad-out')

  # The main compile step.
  with Step('scons_compile', status):
    SCons(context, parallel=True, args=[])

  if context['coverage']:
    with Step('collect_coverage', status, halt_on_fail=True):
      SCons(context, args=['coverage'])
    with Step('summarize_coverage', status, halt_on_fail=False):
      SummarizeCoverage(context)
    slave_type = os.environ.get('BUILDBOT_SLAVE_TYPE')
    if slave_type != 'Trybot' and slave_type is not None:
      with Step('archive_coverage', status, halt_on_fail=True):
        ArchiveCoverage(context)
    return

  # Android bots don't run tests for now.
  if context['android']:
    return

  ### BEGIN tests ###
  if not context['use_glibc']:
    # Bypassing the IRT with glibc is not a supported case,
    # and in fact does not work at all with the new glibc.
    with Step('small_tests', status, halt_on_fail=False):
      SCons(context, args=['small_tests'])
    with Step('medium_tests', status, halt_on_fail=False):
      SCons(context, args=['medium_tests'])
    with Step('large_tests', status, halt_on_fail=False):
      SCons(context, args=['large_tests'])

  with Step('compile IRT tests', status):
    SCons(context, parallel=True, mode=['nacl_irt_test'])

  with Step('small_tests under IRT', status, halt_on_fail=False):
    SCons(context, mode=context['default_scons_mode'] + ['nacl_irt_test'],
          args=['small_tests_irt'])
  with Step('medium_tests under IRT', status, halt_on_fail=False):
    SCons(context, mode=context['default_scons_mode'] + ['nacl_irt_test'],
          args=['medium_tests_irt'])
  with Step('large_tests under IRT', status, halt_on_fail=False):
    SCons(context, mode=context['default_scons_mode'] + ['nacl_irt_test'],
          args=['large_tests_irt'])
  ### END tests ###

  ### BEGIN GN tests ###
  if can_use_gn:
    arch_name = {
      'arm': 'arm',
      '32': 'x86',
      '64': 'x64'
    }[context['arch']]
    gn_sel_ldr = os.path.join(gn_out, 'trusted_' + arch_name, 'sel_ldr')
    gn_extra = [
        'force_sel_ldr=' + gn_sel_ldr,
        'perf_prefix=gn_',
    ]
    with Step('small_tests under GN', status, halt_on_fail=False):
      SCons(context, args=['small_tests'] + gn_extra)
    with Step('medium_tests under GN', status, halt_on_fail=False):
      SCons(context, args=['medium_tests'] + gn_extra)
    with Step('large_tests under GN', status, halt_on_fail=False):
      SCons(context, args=['large_tests'] + gn_extra)
  ### END GN tests ###


def Main():
  # TODO(ncbray) make buildbot scripts composable to support toolchain use case.
  context = BuildContext()
  status = BuildStatus(context)
  ParseStandardCommandLine(context)
  SetupContextVars(context)
  if context.Windows():
    SetupWindowsEnvironment(context)
  elif context.Linux():
    if context['android']:
      SetupAndroidEnvironment(context)
    else:
      SetupLinuxEnvironment(context)
  elif context.Mac():
    SetupMacEnvironment(context)
  else:
    raise Exception("Unsupported platform.")
  RunBuild(BuildScript, status)


def TimedMain():
  start_time = time.time()
  try:
    Main()
  finally:
    time_taken = time.time() - start_time
    print 'RESULT BuildbotTime: total= %.3f minutes' % (time_taken / 60)


if __name__ == '__main__':
  TimedMain()
