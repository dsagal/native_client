# DEPS files look like -*- Python -*-

vars = {
  "chrome_rev": "250916",
  # NOTE!  These four should match their counterparts in chromium/src/DEPS.
  # Be sure to update them when updating chrome_rev, above.
  # (This is not essential for Breakpad, because we do not use its code
  # in the build that goes into Chromium.  But might as well update it too.)
  "gtest_rev": "643",
  "gyp_rev": "1846",
  "lss_rev": "24",
  "breakpad_rev": "1271",

  "lcov_rev": "149720",
  "tools_rev": "12760",

  # These are URL prefixes rather than revision numbers, so keep them separate.
  "chromium_trunk": "http://src.chromium.org/svn/trunk",
  "googlecode_url": "http://%s.googlecode.com/svn",
  "native_client_trunk": "http://src.chromium.org/native_client/trunk",
}

deps = {
  "breakpad":
    ((Var("googlecode_url") % "google-breakpad") + "/trunk@" +
     Var('breakpad_rev')),
  "testing/gtest":
    "http://googletest.googlecode.com/svn/trunk@" + Var("gtest_rev"),
  "third_party":
    Var("native_client_trunk") + "/src/third_party@" + Var("tools_rev"),
  "validator_snapshots":
    Var("native_client_trunk") + "/src/validator_snapshots@" + Var("tools_rev"),
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
    "http://gyp.googlecode.com/svn/trunk@" + Var("gyp_rev"),
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
  }
}

hooks = [
  {
    "pattern": ".",
    "action": ["python", "native_client/build/download_toolchains.py",
               "--keep", "--arm-untrusted", "native_client/TOOL_REVISIONS"],
  },
  {
    "pattern": ".",
    "action": ["python", "native_client/build/gyp_nacl"],
  },
]

include_rules = [
  "+native_client/src/include",
  "+gtest",
]
