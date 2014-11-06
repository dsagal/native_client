# DEPS files look like -*- Python -*-

vars = {
  "chrome_rev": "275800",
  # NOTE!  These four should match their counterparts in chromium/src/DEPS.
  # Be sure to update them when updating chrome_rev, above.
  # (This is not essential for Breakpad, because we do not use its code
  # in the build that goes into Chromium.  But might as well update it too.)
  "gtest_rev": "643",
  "gyp_rev": "1927",
  "lss_rev": "26",
  "breakpad_rev": "1338",

  "lcov_rev": "149720",
  "tools_rev": "13800",

  "validator_snapshots_rev": "14051",

  # These are URL prefixes rather than revision numbers, so keep them separate.
  "chromium_trunk": "http://src.chromium.org/svn/trunk",
  "googlecode_url": "http://%s.googlecode.com/svn",
  "native_client_trunk": "http://src.chromium.org/native_client/trunk",
  "chromium_git": "https://chromium.googlesource.com",

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling build tools
  # and whatever else without interference from each other.
  "buildtools_revision": "c27f95bd1d9baaef70c879dea375090dd1496147"
}

deps = {
  "breakpad":
    ((Var("googlecode_url") % "google-breakpad") + "/trunk@" +
     Var('breakpad_rev')),
  "buildtools":
    Var("chromium_git") + "/chromium/buildtools.git@" +
     Var("buildtools_revision"),
  "testing/gtest":
    (Var("googlecode_url") % "googletest") + "/trunk@" + Var("gtest_rev"),
  "third_party":
    Var("native_client_trunk") + "/src/third_party@" + Var("tools_rev"),
  "validator_snapshots":
    (Var("native_client_trunk") + "/src/validator_snapshots@" +
     Var("validator_snapshots_rev")),
  "third_party/lcov":
    Var("chromium_trunk") + "/src/third_party/lcov@" + Var("lcov_rev"),
  "third_party/lss":
    ((Var("googlecode_url") % "linux-syscall-support") +
     "/trunk/lss@" + Var("lss_rev")),
  "third_party/valgrind":
    Var("chromium_trunk") + "/deps/third_party/valgrind@" + Var("chrome_rev"),
  "tools/clang":
    Var("chromium_trunk") + "/src/tools/clang@" + Var("chrome_rev"),
  "tools/gyp":
    (Var("googlecode_url") % "gyp") + "/trunk@" + Var("gyp_rev"),
  "tools/valgrind":
    Var("chromium_trunk") + "/src/tools/valgrind@" + Var("chrome_rev"),
}

deps_os = {
  "win": {
    # GNU binutils assembler for x86-32.
    "third_party/gnu_binutils":
      (Var("native_client_trunk") + "/deps/third_party/gnu_binutils@" +
       Var("tools_rev")),
    # GNU binutils assembler for x86-64.
    "third_party/mingw-w64/mingw/bin":
      (Var("native_client_trunk") + "/deps/third_party/mingw-w64/mingw/bin@" +
       Var("tools_rev")),
    "third_party/NSIS":
      (Var("native_client_trunk") + "/deps/third_party/NSIS@" +
       Var("tools_rev")),
  },
  "unix": {
    # Ragel for validator_ragel
    "third_party/ragel":
      (Var("native_client_trunk") + "/deps/third_party/ragel@" +
       Var("tools_rev")),
  },
  "android": {
    "third_party/android_tools":
      Var("chromium_git") + "/android_tools.git" +
      "@aabf2a28449fe47dedeee51d3570ff8687ff015f",
  },
}

hooks = [
  # Pull NaCl Toolchain binaries. This needs to be before running GYP below.
  {
    "pattern": ".",
    "action": ["python",
               "native_client/build/package_version/package_version.py",
               "sync", "--extract",
    ],
  },
  {
    # Update the Windows toolchain if necessary.
    "name": "win_toolchain",
    "pattern": ".",
    "action": ["python", "native_client/build/vs_toolchain.py", "update"],
  },
  # Update clang
  {
    "name": "clang",
    "pattern": ".",
    "action": ["python", "tools/clang/scripts/update.py", "--if-needed"],
  },
  # Pull GN binaries. This needs to be before running GYP below.
  {
    "name": "gn_win",
    "pattern": ".",
    "action": [ "download_from_google_storage",
                "--no_resume",
                "--platform=win32",
                "--no_auth",
                "--bucket", "chromium-gn",
                "-s", "buildtools/win/gn.exe.sha1",
    ],
  },
  {
    "name": "gn_mac",
    "pattern": ".",
    "action": [ "download_from_google_storage",
                "--no_resume",
                "--platform=darwin",
                "--no_auth",
                "--bucket", "chromium-gn",
                "-s", "buildtools/mac/gn.sha1",
    ],
  },
  {
    "name": "gn_linux32",
    "pattern": ".",
    "action": [ "download_from_google_storage",
                "--no_resume",
                "--platform=linux*",
                "--no_auth",
                "--bucket", "chromium-gn",
                "-s", "buildtools/linux32/gn.sha1",
    ],
  },
  {
    "name": "gn_linux64",
    "pattern": ".",
    "action": [ "download_from_google_storage",
                "--no_resume",
                "--platform=linux*",
                "--no_auth",
                "--bucket", "chromium-gn",
                "-s", "buildtools/linux64/gn.sha1",
    ],
  },
  # Run GYP, do this last to make sure all the tools are present first.
  {
    "pattern": ".",
    "action": ["python", "native_client/build/gyp_nacl"],
  },
]

include_rules = [
  "+native_client/src/include",
  "+gtest",
]
