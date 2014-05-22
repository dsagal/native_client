#!/usr/bin/python
# Copyright (c) 2013 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Recipes for PNaCl toolchain packages.

   Recipes consist of specially-structured dictionaries, with keys for package
   name, type, commands to execute, etc. The structure is documented in the
   PackageBuilder docstring in toolchain_main.py.

   The real entry plumbing and CLI flags are also in toolchain_main.py.
"""

import fnmatch
import logging
import os
import shutil
import sys
import zipfile

sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
import pynacl.gsd_storage
import pynacl.platform
import pynacl.repo_tools

import command
import pnacl_commands
import pnacl_targetlibs
import toolchain_main

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
NACL_DIR = os.path.dirname(SCRIPT_DIR)
# Use the argparse from third_party to ensure it's the same on all platorms
python_lib_dir = os.path.join(os.path.dirname(NACL_DIR), 'third_party',
                              'python_libs', 'argparse')
sys.path.insert(0, python_lib_dir)
import argparse

# Scons tests can check this version number to decide whether to enable tests
# for toolchain bug fixes or new features.  This allows tests to be enabled on
# the toolchain buildbots/trybots before the new toolchain version is pinned
# (i.e. before the tests would pass on the main NaCl buildbots/trybots).
# If you are adding a test that depends on a toolchain change, you can
# increment this version number manually.
FEATURE_VERSION = 5

# For backward compatibility, these key names match the directory names
# previously used with gclient
GIT_REPOS = {
    'binutils': 'nacl-binutils.git',
    'clang': 'pnacl-clang.git',
    'llvm': 'pnacl-llvm.git',
    'gcc': 'pnacl-gcc.git',
    'libcxx': 'pnacl-libcxx.git',
    'libcxxabi': 'pnacl-libcxxabi.git',
    'nacl-newlib': 'nacl-newlib.git',
    'llvm-test-suite': 'pnacl-llvm-testsuite.git',
    'compiler-rt': 'pnacl-compiler-rt.git',
    }

GIT_BASE_URL = 'https://chromium.googlesource.com/native_client/'
GIT_DEPS_FILE = os.path.join(NACL_DIR, 'pnacl', 'COMPONENT_REVISIONS')

# TODO(dschuff): Some of this mingw logic duplicates stuff in command.py
BUILD_CROSS_MINGW = False
# Path to the mingw cross-compiler libs on Ubuntu
CROSS_MINGW_LIBPATH = '/usr/lib/gcc/i686-w64-mingw32/4.6'
# Path and version of the native mingw compiler to be installed on Windows hosts
MINGW_PATH = os.path.join(NACL_DIR, 'mingw32')
MINGW_VERSION = 'i686-w64-mingw32-4.8.1'

ALL_ARCHES = ('x86-32', 'x86-64', 'arm', 'mips32',
              'x86-32-nonsfi', 'arm-nonsfi')
# MIPS32 doesn't use biased bitcode, and nonsfi targets don't need it.
BITCODE_BIASES = tuple(bias for bias in ('portable', 'x86-32', 'x86-64', 'arm'))

MAKE_DESTDIR_CMD = ['make', 'DESTDIR=%(abs_output)s']

def TripleIsWindows(t):
  return fnmatch.fnmatch(t, '*-mingw32*')

def TripleIsCygWin(t):
  return fnmatch.fnmatch(t, '*-cygwin*')


def CompilersForHost(host):
  compiler = {
      # For now we only do native builds for linux and mac
      'i686-linux': ('gcc', 'g++'), # treat 32-bit linux like a native build
      'x86_64-linux': ('gcc', 'g++'),
      # TODO(dschuff): switch to clang on mac
      'x86_64-apple-darwin': ('gcc', 'g++'),
      # Windows build should work for native and cross
      'i686-w64-mingw32': ('i686-w64-mingw32-gcc', 'i686-w64-mingw32-g++'),
      # TODO: add arm-hosted support
      'i686-pc-cygwin': ('gcc', 'g++'),
  }
  return compiler[host]


def ConfigureHostArchFlags(host):
  configure_args = []
  extra_cc_args = []

  native = pynacl.platform.PlatformTriple()
  is_cross = host != native
  if is_cross:
    if (pynacl.platform.IsLinux64() and
        fnmatch.fnmatch(host, '*-linux*')):
      # 64 bit linux can build 32 bit linux binaries while still being a native
      # build for our purposes. But it's not what config.guess will yield, so
      # use --build to force it and make sure things build correctly.
      configure_args.append('--build=' + host)
      extra_cc_args = ['-m32']
    else:
      configure_args.append('--host=' + host)

  extra_cxx_args = list(extra_cc_args)

  cc, cxx = CompilersForHost(host)

  if is_cross:
    # LLVM's linux->mingw cross build needs this
    configure_args.append('CC_FOR_BUILD=gcc')

  configure_args.append('CC=' + ' '.join([cc] + extra_cc_args))
  configure_args.append('CXX=' + ' '.join([cxx] + extra_cxx_args))

  if TripleIsWindows(host):
    # The i18n support brings in runtime dependencies on MinGW DLLs
    # that we don't want to have to distribute alongside our binaries.
    # So just disable it, and compiler messages will always be in US English.
    configure_args.append('--disable-nls')
    configure_args.extend(['LDFLAGS=-L%(abs_libdl)s',
                           'CFLAGS=-isystem %(abs_libdl)s',
                           'CXXFLAGS=-isystem %(abs_libdl)s'])
  return configure_args


def CmakeHostArchFlags(host, options):
  cmake_flags = []
  if options.clang:
    cc ='%(abs_top_srcdir)s/../third_party/llvm-build/Release+Asserts/bin/clang'
    cxx = cc + '++'
  else:
    cc, cxx = CompilersForHost(host)
  cmake_flags.extend(['-DCMAKE_C_COMPILER='+cc, '-DCMAKE_CXX_COMPILER='+cxx])

  if pynacl.platform.IsLinux64() and pynacl.platform.PlatformTriple() != host:
    # Currently the only supported "cross" build is 64-bit Linux to 32-bit
    # Linux. Enable it.  Also disable libxml and libtinfo because our Ubuntu
    # doesn't have 32-bit libxml or libtinfo build, and users may not have them
    # either.
    cmake_flags.extend(['-DLLVM_BUILD_32_BITS=ON',
                        '-DLLVM_ENABLE_LIBXML=OFF',
                        '-DLLVM_ENABLE_TERMINFO=OFF'])

  if options.sanitize:
    cmake_flags.extend(['-DCMAKE_%s_FLAGS=-fsanitize=%s' % (c, options.sanitize)
                        for c in ('C', 'CXX')])
    cmake_flags.append('-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=%s' %
                       options.sanitize)
  return cmake_flags


def MakeCommand(host):
  make_command = ['make']
  if not pynacl.platform.IsWindows() or pynacl.platform.IsCygWin():
    # The make that ships with msys sometimes hangs when run with -j.
    # The ming32-make that comes with the compiler itself reportedly doesn't
    # have this problem, but it has issues with pathnames with LLVM's build.
    make_command.append('-j%(cores)s')

  if TripleIsWindows(host):
    # There appears to be nothing we can pass at top-level configure time
    # that will prevent the configure scripts from finding MinGW's libiconv
    # and using it.  We have to force this variable into the environment
    # of the sub-configure runs, which are run via make.
    make_command.append('HAVE_LIBICONV=no')
  return make_command


def CopyWindowsHostLibs(host):

  if not TripleIsWindows(host) and not TripleIsCygWin(host):
    return []

  if TripleIsCygWin(host):
    lib_path = '/bin'
    libs = ('cyggcc_s-1.dll', 'cygiconv-2.dll', 'cygwin1.dll',
            'cygintl-8.dll', 'cygstdc++-6.dll', 'cygz.dll')
  elif pynacl.platform.IsWindows():
    lib_path = os.path.join(MINGW_PATH, 'bin')
    # The native minGW compiler uses winpthread, but the Ubuntu cross compiler
    # does not.
    libs = ('libgcc_s_sjlj-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')
  else:
    lib_path = os.path.join(CROSS_MINGW_LIBPATH)
    libs = ('libgcc_s_sjlj-1.dll', 'libstdc++-6.dll')
  return [command.Copy(
                  os.path.join(lib_path, lib),
                  os.path.join('%(output)s', 'bin', lib))
               for lib in libs]

def GetGitSyncCmdsCallback(revisions):
  """Return a callback which returns the git sync commands for a component.

     This allows all the revision information to be processed here while giving
     other modules like pnacl_targetlibs.py the ability to define their own
     source targets with minimal boilerplate.
  """
  def GetGitSyncCmds(component):
    git_url = GIT_BASE_URL + GIT_REPOS[component]
    return (command.SyncGitRepoCmds(git_url, '%(output)s',
                                    revisions[component]) +
            [command.Runnable(None,
                              pnacl_commands.CmdCheckoutGitBundleForTrybot,
                              component, '%(output)s')])

  return GetGitSyncCmds

def HostToolsSources(GetGitSyncCmds):
  sources = {
      'binutils_pnacl_src': {
          'type': 'source',
          'output_dirname': 'binutils',
          'commands': GetGitSyncCmds('binutils'),
      },
      # For some reason, the llvm build using --with-clang-srcdir chokes if the
      # clang source directory is named something other than 'clang', so don't
      # change output_dirname for clang.
      'clang_src': {
          'type': 'source',
          'output_dirname': 'clang',
          'commands': GetGitSyncCmds('clang'),
      },
      'llvm_src': {
          'type': 'source',
          'output_dirname': 'llvm',
          'commands': GetGitSyncCmds('llvm'),
      },
  }
  return sources


def TestsuiteSources(GetGitSyncCmds):
  sources = {
      'llvm_testsuite_src': {
          'type': 'source',
          'output_dirname': 'llvm-test-suite',
          'commands': GetGitSyncCmds('llvm-test-suite'),
      },
  }
  return sources


def HostLibs(host):
  libs = {}
  if TripleIsWindows(host):
    if pynacl.platform.IsWindows():
      ar = 'ar'
    else:
      ar = 'i686-w64-mingw32-ar'

    libs.update({
      'libdl': {
          'type': 'build',
          'inputs' : { 'src' : os.path.join(NACL_DIR, '..', 'third_party',
                                            'dlfcn-win32') },
          'commands': [
              command.CopyTree('%(src)s', '.'),
              command.Command(['i686-w64-mingw32-gcc',
                               '-o', 'dlfcn.o', '-c', 'dlfcn.c',
                               '-Wall', '-O3', '-fomit-frame-pointer']),
              command.Command([ar, 'cru',
                               'libdl.a', 'dlfcn.o']),
              command.Copy('libdl.a',
                           os.path.join('%(output)s', 'libdl.a')),
              command.Copy('dlfcn.h',
                           os.path.join('%(output)s', 'dlfcn.h')),
          ],
      },
    })
  return libs


def HostTools(host, options):
  def H(component_name):
    # Return a package name for a component name with a host triple.
    return component_name + '_' + pynacl.gsd_storage.LegalizeName(host)
  def IsHost64(host):
    return fnmatch.fnmatch(host, 'x86_64*')
  def HostSubdir(host):
    return 'host_x86_64' if IsHost64(host) else 'host_x86_32'
  def BinSubdir(host):
    return 'bin64' if host == 'x86_64-linux' else 'bin'
  # Return the file name with the appropriate suffix for an executable file.
  def Exe(file):
    if TripleIsWindows(host):
      return file + '.exe'
    else:
      return file
  tools = {
      H('binutils_pnacl'): {
          'dependencies': ['binutils_pnacl_src'],
          'type': 'build',
          'output_subdir': HostSubdir(host),
          'commands': [
              command.SkipForIncrementalCommand([
                  'sh',
                  '%(binutils_pnacl_src)s/configure'] +
                  ConfigureHostArchFlags(host) +
                  ['--prefix=',
                  '--disable-silent-rules',
                  '--target=arm-pc-nacl',
                  '--program-prefix=le32-nacl-',
                  '--enable-targets=arm-pc-nacl,i686-pc-nacl,x86_64-pc-nacl,' +
                  'mipsel-pc-nacl',
                  '--enable-deterministic-archives',
                  '--enable-shared=no',
                  '--enable-gold=default',
                  '--enable-ld=no',
                  '--enable-plugins',
                  '--without-gas',
                  '--without-zlib',
                  '--with-sysroot=/arm-pc-nacl']),
              command.Command(MakeCommand(host)),
              command.Command(MAKE_DESTDIR_CMD + ['install-strip'])] +
              [command.RemoveDirectory(os.path.join('%(output)s', dir))
               for dir in ('arm-pc-nacl', 'lib', 'lib32')]
      },
      H('driver'): {
        'type': 'build',
        'output_subdir': BinSubdir(host),
        'inputs': { 'src': os.path.join(NACL_DIR, 'pnacl', 'driver')},
        'commands': [
            command.Runnable(
                None,
                pnacl_commands.InstallDriverScripts,
                '%(src)s', '%(output)s',
                host_windows=TripleIsWindows(host) or TripleIsCygWin(host),
                host_64bit=IsHost64(host))
        ],
      },
  }
  llvm_cmake = {
      H('llvm'): {
          'dependencies': ['clang_src', 'llvm_src', 'binutils_pnacl_src'],
          'type': 'build',
          'output_subdir': HostSubdir(host),
          'commands': [
              command.SkipForIncrementalCommand([
                  'cmake', '-G', 'Ninja'] +
                  CmakeHostArchFlags(host, options) +
                  ['-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                  '-DCMAKE_INSTALL_PREFIX=%(output)s',
                  '-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib',
                  '-DBUILD_SHARED_LIBS=ON',
                  '-DLLVM_ENABLE_ASSERTIONS=ON',
                  '-DLLVM_ENABLE_ZLIB=OFF',
                  '-DLLVM_BUILD_TESTS=ON',
                  '-DLLVM_APPEND_VC_REV=ON',
                  '-DLLVM_BINUTILS_INCDIR=%(abs_binutils_pnacl_src)s/include',
                  '-DLLVM_EXTERNAL_CLANG_SOURCE_DIR=%(clang_src)s',
                  '%(llvm_src)s']),
              command.Command(['ninja', '-v']),
              command.Command(['ninja', 'install']),
        ],
      },
  }
  llvm_autoconf = {
      H('llvm'): {
          'dependencies': ['clang_src', 'llvm_src', 'binutils_pnacl_src'],
          'type': 'build',
          'output_subdir': HostSubdir(host),
          'commands': [
              command.SkipForIncrementalCommand([
                  'sh',
                  '%(llvm_src)s/configure'] +
                  ConfigureHostArchFlags(host) +
                  ['--prefix=/',
                   '--enable-shared',
                   '--disable-zlib',
                   '--disable-terminfo',
                   '--disable-jit',
                   '--disable-bindings', # ocaml is currently the only binding.
                   '--with-binutils-include=%(abs_binutils_pnacl_src)s/include',
                   '--enable-targets=x86,arm,mips',
                   '--program-prefix=',
                   '--enable-optimized',
                   '--with-clang-srcdir=%(abs_clang_src)s']),
              command.Command(MakeCommand(host) + [
                  'VERBOSE=1',
                  'NACL_SANDBOX=0',
                  'all']),
              command.Command(MAKE_DESTDIR_CMD + ['install']),
              command.Remove(*[os.path.join('%(output)s', 'lib', f) for f in
                              '*.a', '*Hello.*', 'BugpointPasses.*']),
              command.Remove(*[os.path.join('%(output)s', 'bin', f) for f in
                               Exe('clang-format'), Exe('clang-check'),
                               Exe('c-index-test'), Exe('clang-tblgen'),
                               Exe('llvm-tblgen')])] +
              CopyWindowsHostLibs(host),
      },
  }
  if options.cmake:
    tools.update(llvm_cmake)
  else:
    tools.update(llvm_autoconf)
  if TripleIsWindows(host):
    tools[H('binutils_pnacl')]['dependencies'].append('libdl')
    tools[H('llvm')]['dependencies'].append('libdl')
  return tools

# TODO(dschuff): The REV file should probably go here rather than in the driver
# dir
def Metadata():
  data = {
      'metadata': {
          'type': 'build',
          'inputs': { 'readme': os.path.join(NACL_DIR, 'pnacl', 'README') },
          'commands': [
              command.Copy('%(readme)s', os.path.join('%(output)s', 'README')),
              command.WriteData(str(FEATURE_VERSION),
                                os.path.join('%(output)s', 'FEATURE_VERSION')),
          ],
      }
  }
  return data

def ParseComponentRevisionsFile(filename):
  ''' Parse a simple-format deps file, with fields of the form:
key=value
Keys should match the keys in GIT_REPOS above, which match the previous
directory names used by gclient (with the exception that '_' in the file is
replaced by '-' in the returned key name).
Values are the git hashes for each repo.
Empty lines or lines beginning with '#' are ignored.
This function returns a dictionary mapping the keys found in the file to their
values.
'''
  with open(filename) as f:
    deps = {}
    for line in f:
      stripped = line.strip()
      if stripped.startswith('#') or len(stripped) == 0:
        continue
      tokens = stripped.split('=')
      if len(tokens) != 2:
        raise Exception('Malformed component revisions file: ' + filename)
      deps[tokens[0].replace('_', '-')] = tokens[1]
  return deps


# This is to replace build.sh's use of gclient, which will eliminate the issues
# with msys vs cygwin git checkouts, and make testing easier. Note that the new
# build scripts will not share source directories with build.sh.
def GetGitSyncPNaClReposCmdsCallback(revisions):
  def GetGitSyncCmds(repo):
    git_url = GIT_BASE_URL + GIT_REPOS[repo]

    # This replaces build.sh's newlib-nacl-headers-clean step by cleaning the
    # the newlib repo on checkout (while silently blowing away any local
    # changes). TODO(dschuff): find a better way to handle nacl newlib headers.
    is_newlib = repo == 'nacl-newlib'
    return (command.SyncGitRepoCmds(git_url, '%(output)s', revisions[repo],
                                    clean=is_newlib) +
            [command.Runnable(None,
                              pnacl_commands.CmdCheckoutGitBundleForTrybot,
                              repo, '%(output)s')])

  return GetGitSyncCmds


def GetSyncPNaClReposSource(revisions, GetGitSyncCmds):
  sources = {}
  for repo, revision in revisions.iteritems():
    sources['legacy_pnacl_%s_src' % repo] = {
        'type': 'source',
        'output_dirname': os.path.join(NACL_DIR, 'pnacl', 'git', repo),
        'commands': GetGitSyncCmds(repo),
    }
  return sources


def InstallMinGWHostCompiler():
  """Install the MinGW host compiler used to build the host tools on Windows.

  We could use an ordinary source rule for this, but that would require hashing
  hundreds of MB of toolchain files on every build. Instead, check for the
  presence of the specially-named file <version>.installed in the install
  directory. If it is absent, check for the presence of the zip file
  <version>.zip. If it is absent, attempt to download it from Google Storage.
  Then extract the zip file and create the install file.
  """
  if not os.path.isfile(os.path.join(MINGW_PATH, MINGW_VERSION + '.installed')):
    downloader = pynacl.gsd_storage.GSDStorage([], ['nativeclient-mingw'])
    zipfilename = MINGW_VERSION + '.zip'
    zipfilepath = os.path.join(NACL_DIR, zipfilename)
    # If the zip file is not present, try to download it from Google Storage.
    # If that fails, bail out.
    if (not os.path.isfile(zipfilepath) and
        not downloader.GetSecureFile(zipfilename, zipfilepath)):
        print >>sys.stderr, 'Failed to install MinGW tools:'
        print >>sys.stderr, 'could not find or download', zipfilename
        sys.exit(1)
    logging.info('Extracting %s' % zipfilename)
    zf = zipfile.ZipFile(zipfilepath)
    if os.path.exists(MINGW_PATH):
      shutil.rmtree(MINGW_PATH)
    zf.extractall(NACL_DIR)
    with open(os.path.join(MINGW_PATH, MINGW_VERSION + '.installed'), 'w') as _:
      pass
  os.environ['MINGW'] = MINGW_PATH


def GetUploadPackageTargets():
  """Package Targets describes all the archived package targets.

  This build can be built among many build bots, but eventually all things
  will be combined together. This package target dictionary describes the final
  output of the entire build.
  """
  package_targets = {}

  common_packages = ['metadata']

  # Target native libraries
  for arch in ALL_ARCHES:
    legal_arch = pynacl.gsd_storage.LegalizeName(arch)
    common_packages.append('libs_support_native_%s' % legal_arch)
    common_packages.append('compiler_rt_%s' % legal_arch)
    if not 'nonsfi' in arch:
      common_packages.append('libgcc_eh_%s' % legal_arch)

  # Target bitcode libraries
  for bias in BITCODE_BIASES:
    legal_bias = pynacl.gsd_storage.LegalizeName(bias)
    common_packages.append('newlib_%s' % legal_bias)
    common_packages.append('libcxx_%s' % legal_bias)
    common_packages.append('libstdcxx_%s' % legal_bias)
    common_packages.append('libs_support_bitcode_%s' % legal_bias)

  # Host components
  host_packages = {}
  for os_name, arch in (('cygwin', 'x86-32'),
                        ('mac', 'x86-64'),
                        ('linux', 'x86-32'),
                        ('linux', 'x86-64')):
    triple = pynacl.platform.PlatformTriple(os_name, arch)
    legal_triple = pynacl.gsd_storage.LegalizeName(triple)
    host_packages.setdefault(os_name, []).extend(
        ['binutils_pnacl_%s' % legal_triple,
         'llvm_%s' % legal_triple,
         'driver_%s' % legal_triple])

  # Unsandboxed target IRT libraries
  for os_name in ('linux', 'mac'):
    legal_triple = pynacl.gsd_storage.LegalizeName('x86-32-' + os_name)
    host_packages[os_name].append('unsandboxed_irt_%s' % legal_triple)

  for os_name, os_packages in host_packages.iteritems():
    package_target = '%s_x86' % pynacl.platform.GetOS(os_name)
    package_targets[package_target] = {}
    package_name = 'pnacl_newlib'
    combined_packages = os_packages + common_packages
    package_targets[package_target][package_name] = combined_packages

  return package_targets

if __name__ == '__main__':
  # This sets the logging for gclient-alike repo sync. It will be overridden
  # by the package builder based on the command-line flags.
  logging.getLogger().setLevel(logging.DEBUG)
  parser = argparse.ArgumentParser(add_help=False)
  parser.add_argument('--legacy-repo-sync', action='store_true',
                      dest='legacy_repo_sync', default=False,
                      help='Sync the git repo directories used by build.sh')
  parser.add_argument('--build-64bit-host', action='store_true',
                      dest='build_64bit_host', default=False,
                      help='Build 64-bit Linux host binaries in addition to 32')
  parser.add_argument('--cmake', action='store_true', default=False,
                      help="Use LLVM's cmake ninja build instead of autoconf")
  parser.add_argument('--clang', action='store_true', default=False,
                      help="Use clang instead of gcc with LLVM's cmake build")
  parser.add_argument('--sanitize', choices=['address', 'thread', 'undefined'],
                      help="Use a sanitizer with LLVM's clang cmake build")
  parser.add_argument('--testsuite-sync', action='store_true', default=False,
                      help=('Sync the sources for the LLVM testsuite. '
                      'Only useful if --sync/ is also enabled'))
  args, leftover_args = parser.parse_known_args()
  if '-h' in leftover_args or '--help' in leftover_args:
    print 'The following arguments are specific to toolchain_build_pnacl.py:'
    parser.print_help()
    print 'The rest of the arguments are generic, in toolchain_main.py'

  if args.sanitize and not args.cmake:
    print 'Use of sanitizers requires a cmake build'
    sys.exit(1)
  if args.clang and not args.cmake:
    print 'Use of clang is currently only supported with cmake/ninja'
    sys.exit(1)


  packages = {}
  upload_packages = {}

  rev = ParseComponentRevisionsFile(GIT_DEPS_FILE)
  if args.legacy_repo_sync:
    packages = GetSyncPNaClReposSource(rev,
                                       GetGitSyncPNaClReposCmdsCallback(rev))
    # Make sure sync is inside of the args to toolchain_main.
    if not set(['-y', '--sync', '--sync-only']).intersection(leftover_args):
      leftover_args.append('--sync-only')
  else:
    upload_packages = GetUploadPackageTargets()
    if pynacl.platform.IsWindows():
      InstallMinGWHostCompiler()

    packages.update(HostToolsSources(GetGitSyncCmdsCallback(rev)))
    if args.testsuite_sync:
      packages.update(TestsuiteSources(GetGitSyncCmdsCallback(rev)))

    if pynacl.platform.IsLinux64():
      hosts = ['i686-linux']
      if args.build_64bit_host:
        hosts.append(pynacl.platform.PlatformTriple())
    else:
      hosts = [pynacl.platform.PlatformTriple()]
    if pynacl.platform.IsLinux() and BUILD_CROSS_MINGW:
      hosts.append(pynacl.platform.PlatformTriple('win', 'x86-32'))
    for host in hosts:
      packages.update(HostLibs(host))
      packages.update(HostTools(host, args))
    # Don't build the target libs on Windows because of pathname issues.
    # Don't build the target libs on Mac because the gold plugin's rpaths
    # aren't right.
    # On linux use the 32-bit compiler to build the target libs since that's
    # what most developers will be using. (hosts[0] is i686-linux on linux64)
    # TODO(dschuff): Figure out a better way to test things on toolchain bots.
    if pynacl.platform.IsLinux():
      packages.update(pnacl_targetlibs.TargetLibsSrc(
        GetGitSyncCmdsCallback(rev)))
      for bias in BITCODE_BIASES:
        packages.update(pnacl_targetlibs.BitcodeLibs(hosts[0], bias))
      for arch in ALL_ARCHES:
        packages.update(pnacl_targetlibs.NativeLibs(hosts[0], arch))
      packages.update(Metadata())
    if pynacl.platform.IsLinux() or pynacl.platform.IsMac():
      packages.update(pnacl_targetlibs.UnsandboxedIRT(
          'x86-32-%s' % pynacl.platform.GetOS()))


  tb = toolchain_main.PackageBuilder(packages,
                                     upload_packages,
                                     leftover_args)
  tb.Main()
