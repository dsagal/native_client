#!/usr/bin/python
# Copyright (c) 2012 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Enable 'with' statements in Python 2.5
from __future__ import with_statement

import os.path
import re
import subprocess
import sys
import time

from buildbot_lib import (
    BuildContext, BuildStatus, Command, EnsureDirectoryExists,
    ParseStandardCommandLine, RemoveDirectory, RemoveGypBuildDirectories,
    RemoveSconsBuildDirectories, RunBuild, SCons, SetupLinuxEnvironment,
    SetupMacEnvironment, SetupWindowsEnvironment, SetupAndroidEnvironment,
    Step, StepLink, StepText, TryToCleanContents)


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
  if context.Windows():
    Command(
        context,
        cmd=[os.path.join(context['msvc'], 'Common7', 'IDE', 'devenv.com'),
             r'build\all.sln',
             '/build', context['gyp_mode']])
  elif context.Linux():
    Command(context, cmd=[
        'ninja', '-v', '-k', '0', '-C', '../out/' + context['gyp_mode']])
  elif context.Mac():
    Command(context, cmd=[
        'ninja', '-v', '-k', '0', '-C', '../out/' + context['gyp_mode']])
  else:
    raise Exception('Unknown platform')


def CommandGypGenerate(context):
  Command(
          context,
          cmd=[
              sys.executable,
              'native_client/build/gyp_nacl',
              'native_client/build/all.gyp',
              ],
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
    # a) C:\temp\83C4.tmp
    # b) /tmp/.org.chromium.Chromium.EQrEzl
    file_name_re = re.compile(
        r'[\\/]([0-9a-fA-F]+\.tmp|\.org\.chrom\w+\.Chrom\w+\..+)$')
    file_name_filter = lambda fn: file_name_re.search(fn) is not None
    TryToCleanContents(tmp_dir, file_name_filter)

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
  gn_path = None
  if context.Linux() and context['arch'] != 'arm':
    gn_path = '../buildtools/linux32/gn'
    targets = ['trusted_' + context['arch'], 'untrusted']

  # TODO(noelallen) Disable Mac and Windows build, but
  # check in so we can debug on the bots.
  if 0:
    if context.Mac():
      gn_path = '../buildtools/mac/gn'
      targets = ['trusted_' + context['arch'], 'untrusted']
    if context.Windows():
      gn_path = '../buildtools/win/gn.exe'
      targets = ['trusted_' + context['arch'], 'untrusted']

  if gn_path:
    with Step('gn_compile', status):
      Command(context,
              cmd=[gn_path, '--dotfile=../native_client/.gn',
                   '--root=..', 'gen', '../out'])
      Command(context, cmd=['ninja', '-C', '../out', '-j10'] + targets)

  if context['clang']:
    with Step('update_clang', status):
      Command(context, cmd=['../tools/clang/scripts/update.sh'])

  # Just build both bitages of validator and test for --validator mode.
  if context['validator']:
    with Step('build ncval-x86-32', status):
      SCons(context, platform='x86-32', parallel=True, args=['ncval'])
    with Step('build ncval-x86-64', status):
      SCons(context, platform='x86-64', parallel=True, args=['ncval'])

    with Step('build ragel_validator-32', status):
      SCons(context, platform='x86-32', parallel=True, args=['ncval_new'])
    with Step('build ragel_validator-64', status):
      SCons(context, platform='x86-64', parallel=True, args=['ncval_new'])

    with Step('predownload validator corpus', status):
      Command(context,
          cmd=[sys.executable,
               'tests/abi_corpus/validator_regression_test.py',
               '--download-only'])

    with Step('validator_regression_test current x86-32', status,
        halt_on_fail=False):
      ValidatorTest(
          context, 'x86-32', 'scons-out/opt-linux-x86-32/staging/ncval')
    with Step('validator_regression_test current x86-64', status,
        halt_on_fail=False):
      ValidatorTest(
          context, 'x86-64', 'scons-out/opt-linux-x86-64/staging/ncval')

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

    with Step('validator_diff32_tests', status, halt_on_fail=False):
      SCons(context, platform='x86-32', args=['validator_diff_tests'])
    with Step('validator_diff64_tests', status, halt_on_fail=False):
      SCons(context, platform='x86-64', args=['validator_diff_tests'])
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
      Command(context, cmd=['make', '-j%d' % context['max_jobs']],
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
  ### END tests ###

  ### BEGIN GN tests ###
  if gn_path:
    arch_name = {
      'arm': 'arm',
      '32': 'x86',
      '64': 'x64'
    }[context['arch']]
    gn_sel_ldr = '../out/trusted_%s/sel_ldr' % arch_name
    with Step('small_tests under GN', status, halt_on_fail=False):
      SCons(context, args=['small_tests', 'force_sel_ldr=' + gn_sel_ldr])
    with Step('medium_tests under GN', status, halt_on_fail=False):
      SCons(context, args=['medium_tests', 'force_sel_ldr=' + gn_sel_ldr])
    with Step('large_tests under GN', status, halt_on_fail=False):
      SCons(context, args=['large_tests', 'force_sel_ldr=' + gn_sel_ldr])
  ### END GN tests ###

  if not context['no_gyp']:
    # Build with ragel-based validator using GYP.
    gyp_defines_save = context.GetEnv('GYP_DEFINES')
    context.SetEnv('GYP_DEFINES',
                   ' '.join([gyp_defines_save, 'nacl_validator_ragel=0']))
    with Step('gyp_compile_noragel', status):
      # Clobber GYP build to recompile necessary files with new preprocessor macro
      # definitions.  It is done because some build systems (such as GNU Make,
      # MSBuild etc.) do not consider compiler arguments as a dependency.
      RemoveGypBuildDirectories()
      CommandGypGenerate(context)
      CommandGypBuild(context)
    context.SetEnv('GYP_DEFINES', gyp_defines_save)

  # Build with ragel-based validator using scons.
  with Step('scons_compile_noragel', status):
    SCons(context, parallel=True, args=['validator_ragel=0'])

  # Smoke tests for the R-DFA validator.
  with Step('validator_noragel_tests', status):
    args = ['validator_ragel=0',
            'small_tests',
            'medium_tests',
            'large_tests',
            ]
    # Add nacl_irt_test mode to be able to run run_hello_world_test_irt that
    # tests validation of the IRT.
    SCons(context,
          mode=context['default_scons_mode'] + ['nacl_irt_test'],
          args=args)


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
