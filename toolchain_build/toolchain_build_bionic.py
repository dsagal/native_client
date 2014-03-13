#!/usr/bin/python
# Copyright (c) 2012 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Recipes for NativeClient toolchain packages.

The real entry plumbing is in toolchain_main.py.
"""

import fnmatch
import os
import optparse
import process
import stat
import shutil
import StringIO
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
import pynacl.hashing_tools
import pynacl.repo_tools

import toolchain_build
import toolchain_main

from file_update import Mkdir, Rmdir, Symlink
from file_update import NeedsUpdate, UpdateFromTo, UpdateText


BIONIC_VERSION = '649e9f7df39f73cd13b83e8c3371d3f47022ef62'
ARCHES = ['arm']

BUILD_SCRIPT = os.path.abspath(__file__)
TOOLCHAIN_BUILD = os.path.dirname(BUILD_SCRIPT)
TOOLCHAIN_BUILD_SRC = os.path.join(TOOLCHAIN_BUILD, 'src')
TOOLCHAIN_BUILD_OUT = os.path.join(TOOLCHAIN_BUILD, 'out')

BIONIC_SRC = os.path.join(TOOLCHAIN_BUILD_SRC, 'bionic')

NATIVE_CLIENT = os.path.dirname(TOOLCHAIN_BUILD)
TOOLCHAIN = os.path.join(NATIVE_CLIENT, 'toolchain')

ARM_NEWLIB = os.path.join(TOOLCHAIN, 'linux_arm_newlib')
ARM_BIONIC = os.path.join(TOOLCHAIN, 'linux_arm_bionic')
X86_NEWLIB = os.path.join(TOOLCHAIN, 'linux_x86_newlib')
X86_BIONIC = os.path.join(TOOLCHAIN, 'linux_x86_bionic')


PROJECTS = [
  'bionic_%s_work',
  'gcc_%s_work',
]


def ReplaceText(text, maplist):
  for m in maplist:
    for key in m:
      text = text.replace(key, m[key])
  return text


def ReplaceArch(text, arch, subarch=None):
  NACL_ARCHES = {
    'arm': 'arm',
    'x86': 'x86_64'
  }
  GCC_ARCHES = {
    'arm': 'arm',
    'x86': 'i686'
  }
  CPU_ARCHES = {
    'arm': 'arm',
    'x86': 'amd64'
  }
  VERSION_MAP = {
    'arm': '4.8.2',
    'x86': '4.4.3',
  }
  REPLACE_MAP = {
    '$NACL': NACL_ARCHES[arch],
    '$GCC': GCC_ARCHES[arch],
    '$CPU': CPU_ARCHES[arch],
    '$SUB': subarch or '',
    '$ARCH': arch,
    '$VER': VERSION_MAP[arch]
  }
  return ReplaceText(text, [REPLACE_MAP])


def Clobber():
  Rmdir(os.path.join(TOOLCHAIN_BUILD, 'cache'))
  for arch in ARCHES:
    Rmdir(os.path.join(TOOLCHAIN, 'linux_%s_bionic' % arch))
    for workdir in PROJECTS:
      Rmdir(os.path.join(TOOLCHAIN_BUILD_OUT, workdir % arch))


def FetchAndBuild_gcc_libs():
  tc_args = ['-y', '--no-use-remote-cache', 'gcc_libs_arm']
  toolchain_main.PackageBuilder(toolchain_build.PACKAGES, tc_args).Main()


def FetchBionicSources():
  project = 'bionic'
  url = '%s/nacl-%s.git' % (toolchain_build.GIT_BASE_URL, project)
  pynacl.repo_tools.SyncGitRepo(url,
                                os.path.join(TOOLCHAIN_BUILD_SRC, project),
                                BIONIC_VERSION)


def MungeIRT(src, dst):
  replace_map = {
    'off_t': 'int64_t'
  }

  with open(src, 'r') as srcf:
    text = srcf.read()
    text = ReplaceText(text, [replace_map])
    with open(dst, 'w') as dstf:
      dstf.write(text)


def CreateBasicToolchain():
  # Create a toolchain directory containing only the toolchain binaries and
  # basic files line nacl_arm_macros.s.

  UpdateFromTo(ARM_NEWLIB, ARM_BIONIC,
               filters=['*arm-nacl/include*', '*arm-nacl/lib*','*.a', '*.o'])
  UpdateFromTo(ARM_NEWLIB, ARM_BIONIC, paterns=['*.s'])

  #  Static build uses:
  #     crt1.o crti.o 4.8.2/crtbeginT.o ... 4.8.2/crtend.o crtn.o
  # -shared build uses:
  #     crti.o 4.8.2/crtbeginS.o ... 4.8.2/crtendS.o crtn.o crtn.o
  # However we only provide a crtbegin(S) and crtend(S)
  EMPTY = """/*
 * This is a dummy linker script.
 * libnacl.a, libcrt_common.a, crt0.o crt1.o crti.o and crtn.o are all
 * empty.  Instead all the work is done by crtbegin(S).o and crtend(S).o and
 * the bionic libc.  These are provided for compatability with the newlib
 * toolchain binaries.
 */"""
  EMPTY_FILES = ['crt0.o', 'crt1.o', 'crti.o', 'crtn.o',
                 'libnacl.a', 'libcrt_common.a', 'libpthread.a']

  # Bionic uses the following include paths
  BIONIC_PAIRS = [
    ('bionic/libc/include', '$NACL-nacl/include'),
    ('bionic/libc/arch-nacl/syscalls/nacl_stat.h',
        '$NACL-nacl/include/nacl_stat.h'),
    ('bionic/libc/arch-$ARCH/include/machine',
        '$NACL-nacl/include/machine'),
    ('bionic/libc/kernel/common', '$NACL-nacl/include'),
    ('bionic/libc/kernel/arch-$ARCH/asm', '$NACL-nacl/include/asm'),
    ('bionic/libm/include', '$NACL-nacl/include'),
    ('bionic/libm/$CPU', '$NACL-nacl/include'),
    ('bionic/safe-iop/include', '$NACL-nacl/include'),
    ('bionic/libstdc++/nacl',
        '$NACL-nacl/include/c++/4.8.2/$NACL-nacl'),
    ('bionic/nacl/$ARCH', '.'),
  ]


  for arch in ARCHES:
    for name in ['irt.h', 'irt_dev.h']:
      src = os.path.join(NATIVE_CLIENT, 'src', 'untrusted', 'irt', name)
      dst = os.path.join(TOOLCHAIN, 'linux_$ARCH_bionic', '$NACL-nacl',
                       'include', name)
      MungeIRT(src, ReplaceArch(dst, arch))

    inspath = os.path.join(TOOLCHAIN, 'linux_$ARCH_bionic')
    inspath = ReplaceArch(inspath, arch)

    # Create empty objects and libraries
    libpath = ReplaceArch(os.path.join(inspath, '$NACL-nacl', 'lib'), arch)
    for name in EMPTY_FILES:
      UpdateText(os.path.join(libpath, name), EMPTY)

    # Copy BIONIC files to toolchain
    for src, dst in BIONIC_PAIRS:
      srcpath = ReplaceArch(os.path.join(TOOLCHAIN_BUILD_SRC, src), arch)
      dstpath = ReplaceArch(os.path.join(inspath, dst), arch)
      UpdateFromTo(srcpath, dstpath)

    # Build specs file
    gcc = ReplaceArch(os.path.join(inspath, 'bin', '$NACL-nacl-gcc'), arch)
    lib = ReplaceArch(os.path.join(inspath, 'lib/gcc/$NACL-nacl/$VER'), arch)
    specs = os.path.join(lib, 'specs')
    with open(specs, 'w') as specfile:
      process.Run([gcc, '-dumpspecs'], cwd=None, shell=False,
                  outfile=specfile, verbose=False)
    text = open(specs, 'r').read()

    # Replace items in the spec file
    text = ReplaceText(text, [{
      '-lgcc': '-lgcc --as-needed %{!static: -lgcc_s} --no-as-needed %{!shared: -lgcc_eh}',
      '--hash-style=gnu': '--hash-style=sysv',
    }])

    open(specs, 'w').write(text)


def ConfigureAndBuild_libc():
  for arch in ARCHES:
    inspath = os.path.join(TOOLCHAIN, 'linux_$ARCH_bionic')
    inspath = ReplaceArch(inspath, arch)

    workpath = os.path.join(TOOLCHAIN_BUILD_OUT, 'bionic_$ARCH_work')
    workpath = ReplaceArch(workpath, arch)
    ConfigureAndBuild(arch, 'bionic/libc', workpath, inspath, )


def ConfigureAndBuild_libc():
  for arch in ARCHES:
    inspath = os.path.join(TOOLCHAIN, 'linux_$ARCH_bionic')
    inspath = ReplaceArch(inspath, arch)

    workpath = os.path.join(TOOLCHAIN_BUILD_OUT, 'bionic_$ARCH_work')
    workpath = ReplaceArch(workpath, arch)
    ConfigureAndBuild(arch, 'bionic/libc', workpath, inspath)
    ConfigureAndBuild(arch, 'bionic/libm', workpath, inspath)


def ConfigureAndBuildLinker():
  for arch in ARCHES:
    inspath = os.path.join(TOOLCHAIN, 'linux_$ARCH_bionic')
    inspath = ReplaceArch(inspath, arch)

    workpath = os.path.join(TOOLCHAIN_BUILD_OUT, 'bionic_$ARCH_work')
    workpath = ReplaceArch(workpath, arch)
    ConfigureAndBuild(arch, 'bionic/linker', workpath, inspath)


def ConfigureGCCProject(arch, project, cfg, workpath, inspath):
  # configure does not always have +x
  filepath = os.path.abspath(os.path.join(workpath, cfg[0]))
  st_info  = os.stat(filepath)
  os.chmod(filepath, st_info.st_mode | stat.S_IEXEC)

  env = os.environ
  if arch == 'arm':
    newpath = os.path.join(ARM_BIONIC, 'bin') + ':' + env['PATH']
  else:
    newpath = os.path.join(X86_BIONIC, 'bin') + ':' + env['PATH']
  proj = '%s %s' % (project, arch)
  setpath = ['/usr/bin/env', 'PATH=' + newpath]

  # Check if config changed or script is new
  config_path = os.path.join(workpath, 'config.info')
  updated = UpdateText(config_path, ' '.join(cfg))
  updated |= NeedsUpdate(config_path, BUILD_SCRIPT)

  if updated:
    print 'Configure ' + proj
    if process.Run(setpath + cfg, cwd=workpath, env=env, outfile=sys.stdout):
      raise RuntimeError('Failed to configure %s.\n' % proj)
  else:
    print 'Reusing config for %s.' % proj


def MakeGCCProject(arch, project, workpath, targets=[]):
  env = os.environ
  if arch == 'arm':
    newpath = os.path.join(ARM_BIONIC, 'bin') + ':' + env['PATH']
  else:
    newpath = os.path.join(X86_BIONIC, 'bin') + ':' + env['PATH']
  proj = '%s %s' % (project, arch)
  setpath = ['/usr/bin/env', 'PATH=' + newpath]

  if targets:
    proj = project = ': ' + ' '.join(targets)
  else:
    proj = project

  print 'Make ' + proj
  if process.Run(setpath + ['make', '-j16', 'V=1'] + targets,
                  cwd=workpath, outfile=sys.stdout):
    raise RuntimeError('Failed to build %s.\n' % proj)
  print 'Done ' + proj


def ConfigureAndBuild_libgcc(config=False):
  arch = 'arm'
  project = 'libgcc'
  tcpath = os.path.join(TOOLCHAIN, 'linux_$ARCH_bionic')
  tcpath = ReplaceArch(tcpath, arch)

  # Prep work path
  workpath = os.path.join(TOOLCHAIN_BUILD_OUT, 'gcc_$GCC_bionic_work')
  workpath = ReplaceArch(workpath, arch)
  Mkdir(workpath)
  Symlink('../gcc_libs_arm_work/gcc' , os.path.join(workpath, 'gcc'))

  # Prep install path
  inspath = os.path.join(TOOLCHAIN_BUILD_OUT, 'gcc_$GCC_bionic_install')
  inspath = ReplaceArch(inspath, arch)

  dstpath = ReplaceArch(os.path.join(workpath, '$NACL-nacl/libgcc'), arch)
  cfg = [
    '../../../../src/gcc_libs/libgcc/configure',
    '--host=arm-nacl',
    '--build=i686-linux',
    '--target=arm-nacl',
    '--enable-shared',
    '--enable-shared-libgcc',
    '--with-dwarf3',
    '--with-newlib',
    '--prefix=' + inspath,
    'CFLAGS=-I../../../gcc_lib_arm_work'
  ]
  ConfigureGCCProject(arch, project, cfg, dstpath, inspath)
  MakeGCCProject(arch, project, dstpath, ['libgcc.a'])

  # Copy temp version of libgcc.a for linking libc.so
  UpdateFromTo(os.path.join(dstpath, 'libgcc.a'),
               os.path.join(tcpath, 'arm-nacl', 'lib', 'libgcc.a'))


def BuildAndInstall_libgcc_s():
  arch = 'arm'
  project = 'libgcc'
  tcpath = os.path.join(TOOLCHAIN, 'linux_$ARCH_bionic')
  tcpath = ReplaceArch(tcpath, arch)

  # Remove temp copy of libgcc.a, it get's installed at the end
  os.remove(os.path.join(tcpath, 'arm-nacl', 'lib', 'libgcc.a'))

  # Prep work path
  workpath = os.path.join(TOOLCHAIN_BUILD_OUT, 'gcc_$GCC_bionic_work')
  workpath = ReplaceArch(workpath, arch)
  dstpath = ReplaceArch(os.path.join(workpath, '$NACL-nacl/libgcc'), arch)

  # Prep install path
  inspath = os.path.join(TOOLCHAIN_BUILD_OUT, 'gcc_$GCC_bionic_install')
  inspath = ReplaceArch(inspath, arch)

  MakeGCCProject(arch, project, dstpath)
  MakeGCCProject(arch, project, dstpath, ['install'])

  UpdateFromTo(os.path.join(inspath, 'lib', 'gcc'),
               os.path.join(tcpath, 'lib', 'gcc'),
               filters=['*.o'])
  UpdateFromTo(os.path.join(inspath, 'lib', 'libgcc_s.so.1'),
               os.path.join(tcpath, 'arm-nacl', 'lib', 'libgcc_s.so.1'))
  UpdateFromTo(os.path.join(inspath, 'lib', 'libgcc_s.so'),
               os.path.join(tcpath, 'arm-nacl', 'lib', 'libgcc_s.so'))


def ConfigureAndBuild_libstdcpp():
  arch = 'arm'
  project = 'libstdc++'
  tcpath = os.path.join(TOOLCHAIN, 'linux_$ARCH_bionic')
  tcpath = ReplaceArch(tcpath, arch)

  # Prep work path
  workpath = os.path.join(TOOLCHAIN_BUILD_OUT, 'gcc_$GCC_bionic_work')
  workpath = ReplaceArch(workpath, arch)

  # Prep install path
  inspath = os.path.join(TOOLCHAIN_BUILD_OUT, 'gcc_$GCC_bionic_install')
  inspath = ReplaceArch(inspath, arch)

  dstpath = ReplaceArch(os.path.join(workpath, '$NACL-nacl/libstdc++-v3'), arch)
  Mkdir(dstpath)
  cfg = [
    '../../../../src/gcc_libs/libstdc++-v3/configure',
    '--host=arm-nacl',
    '--build=i686-linux',
    '--target=arm-nacl',
    '--enable-shared',
    '--with-newlib',
    '--disable-libstdcxx-pch',
    '--enable-shared-libgcc',
    '--with-dwarf3',
    '--prefix=' + inspath,
    'CFLAGS=-I../../../gcc_lib_arm_work'
  ]

  ConfigureGCCProject(arch, project, cfg, dstpath, inspath)
  MakeGCCProject(arch, project, dstpath)
  MakeGCCProject(arch, project, dstpath, ['install'])

  UpdateFromTo(os.path.join(inspath, 'lib'),
               os.path.join(tcpath, 'arm-nacl', 'lib'))
  UpdateFromTo(os.path.join(inspath, 'include'),
               os.path.join(tcpath, 'arm-nacl', 'include'))


def GetProjectPaths(arch, project):
  srcpath = os.path.join(BIONIC_SRC, project)
  workpath = os.path.join(TOOLCHAIN_BUILD_OUT, 'bionic_$ARCH_work')
  instpath = os.path.join(TOOLCHAIN_BUILD_OUT, 'bionic_$ARCH_install')

  workpath = ReplaceArch(os.path.join(workpath, 'bionic', project), arch)
  toolpath = ReplaceArch(os.path.join(TOOLCHAIN, 'linux_$ARCH_bionic'), arch)
  instpath = ReplaceArch(os.path.join(toolpath, '$NACL-nacl', 'lib'), arch)
  out = {
    'src': srcpath,
    'work': workpath,
    'ins': instpath,
    'tc': toolpath,
  }
  return out


def CreateProject(arch, project, clobber=False):
  paths = GetProjectPaths(arch, project)

  MAKEFILE_TEMPLATE = """
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# GNU Makefile based on shared rules provided by the Native Client SDK.
# See README.Makefiles for more details.

TOOLCHAIN_PATH?=$(tc_path)/linux_$ARCH_bionic
TOOLCHAIN_PREFIX:=$(TOOLCHAIN_PATH)/bin/$GCC-nacl-

CC:=$(TOOLCHAIN_PREFIX)gcc
CXX:=$(TOOLCHAIN_PREFIX)g++
AR:=$(TOOLCHAIN_PREFIX)ar

SRC_ROOT=$(src_path)
DST_ROOT=$(dst_path)
INS_ROOT=$(ins_path)

NACL_ARCH=$NACL
GCC_ARCH=$GCC

MAKEFILE_DEPS:=$(build_tc_path)/tc_bionic.mk
MAKEFILE_DEPS+=$(src_path)/Makefile

include $(build_tc_path)/tc_bionic.mk
include $(src_path)/Makefile
"""
  remap = {
    '$(src_path)': paths['src'],
    '$(dst_path)': paths['work'],
    '$(ins_path)': paths['ins'],
    '$(tc_path)': TOOLCHAIN,
    '$(build_tc_path)': TOOLCHAIN_BUILD
  }
  text = ReplaceText(MAKEFILE_TEMPLATE, [remap])
  text = ReplaceArch(text, arch)

  if clobber:
    print 'Clobering Bionic project directories.'
    Rmdir(paths['work'])

  Mkdir(paths['work'])
  Mkdir(paths['ins'])
  UpdateText(os.path.join(paths['work'], 'Makefile'), text)


def ConfigureBionicProjects(clobber=False):
  PROJECTS = ['libc', 'libm', 'linker', 'tests']
  arch = 'arm'
  for project in PROJECTS:
    print 'Configure %s for %s.' % (project, arch)
    CreateProject(arch, project, clobber)


def MakeBionicProject(project, targets=[], clobber=False):
  arch = 'arm'
  paths = GetProjectPaths(arch, project)
  workpath = paths['work']
  inspath = paths['ins']
  targetlist = ' '.join(targets)

  print 'Building %s for %s at %s %s.' % (project, arch, workpath, targetlist)
  if clobber:
    args = ['make', '-j12', 'V=1', 'clean']
    if process.Run(args, cwd=workpath, outfile=sys.stdout):
      raise RuntimeError('Failed to clean %s for %s.\n' % (project, arch))

  args = ['make', '-j12', 'V=1'] + targets
  if process.Run(args, cwd=workpath, outfile=sys.stdout):
    raise RuntimeError('Failed to build %s for %s.\n' % (project, arch))

  print 'Done with %s for %s.\n' % (project, arch)


def ArchiveAndUpload(version, zipname, zippath):
  if 'BUILDBOT_BUILDERNAME' in os.environ:
    GSUTIL = '../buildbot/gsutil.sh'
  else:
    GSUTIL = 'gsutil'
  GSUTIL_ARGS = [GSUTIL, 'cp', '-a', 'public-read']
  GSUTIL_PATH = 'gs://nativeclient-archive2/toolchain'

  urldir = os.path.join(GSUTIL_PATH, version)
  zipurl = os.path.join(urldir, zipname)
  zipname = os.path.join(TOOLCHAIN_BUILD_OUT, zipname)

  try:
    os.remove(zipname)
  except:
    pass

  sys.stdout.flush()
  print >>sys.stderr, '@@@STEP_LINK@download@%s@@@' % urldir

  if process.Run(['tar', '-czf', zipname, zippath],
                 cwd=TOOLCHAIN,
                 outfile=sys.stdout):
      raise RuntimeError('Failed to zip %s from %s.\n' % (zipname, zippath))

  hashzipname = zipname + '.sha1hash'
  hashzipurl = zipurl + '.sha1hash'
  hashval = pynacl.hashing_tools.HashFileContents(zipname)

  with open(hashzipname, 'w') as f:
    f.write(hashval)

  if process.Run(GSUTIL_ARGS + [zipname, zipurl],
                 cwd=TOOLCHAIN_BUILD,
                 outfile=sys.stdout):
    err = 'Failed to upload zip %s to %s.\n' % (zipname, zipurl)
    raise RuntimeError(err)

  if process.Run(GSUTIL_ARGS + [hashzipname, hashzipurl],
                 cwd=TOOLCHAIN_BUILD,
                 outfile=sys.stdout):
    err = 'Failed to upload hash %s to %s.\n' % (hashzipname, hashzipurl)
    raise RuntimeError(err)


def main(argv):
  parser = optparse.OptionParser()
  parser.add_option(
      '-v', '--verbose', dest='verbose',
      default=False, action='store_true',
      help='Produce more output.')
  parser.add_option(
      '-c', '--clobber', dest='clobber',
      default=False, action='store_true',
      help='Clobber working directories before building.')
  parser.add_option(
      '-s', '--sync', dest='sync',
      default=False, action='store_true',
      help='Sync sources first.')

  parser.add_option(
      '-b', '--buildbot', dest='buildbot',
      default=False, action='store_true',
      help='Running on the buildbot.')

  parser.add_option(
      '-u', '--upload', dest='upload',
      default=False, action='store_true',
      help='Upload build artifacts.')

  parser.add_option(
      '--skip-gcc', dest='skip_gcc',
      default=False, action='store_true',
      help='Skip building GCC components.')

  options, args = parser.parse_args(argv)
  if options.buildbot or options.upload:
    version = os.environ['BUILDBOT_REVISION']

  if options.clobber:
    Clobber()

  if options.sync or options.buildbot:
    FetchBionicSources()

  # Copy headers and compiler tools
  CreateBasicToolchain()

  # Configure Bionic Projects, libc, libm, linker, tests, ...
  ConfigureBionicProjects(clobber=options.buildbot)

  if not options.skip_gcc:
    # Build newlib gcc_libs for use by bionic
    FetchAndBuild_gcc_libs()

  # Configure and build libgcc.a
  ConfigureAndBuild_libgcc()

  # With libgcc.a, we can now build libc.so
  MakeBionicProject('libc')

  # With libc.so, we can build libgcc_s.so
  BuildAndInstall_libgcc_s()

  # With libc and libgcc_s, we can now build libm
  MakeBionicProject('libm')

  # With libc, libgcc, and libm, we can now build libstdc++
  ConfigureAndBuild_libstdcpp()

  # Now we can build the linker
  MakeBionicProject('linker')

  # Now we have a full toolchain, so test it
  MakeBionicProject('tests')

  # We can run only off buildbots
  if not options.buildbot:
    MakeBionicProject('tests', ['run'])

  if options.buildbot or options.upload:
    zipname = 'naclsdk_linux_arm_bionic.tgz'
    ArchiveAndUpload(version, zipname, 'linux_arm_bionic')


if __name__ == '__main__':
  sys.exit(main(sys.argv))