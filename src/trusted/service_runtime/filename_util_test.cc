/*
 * Copyright (c) 2016 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string>
#include <vector>
#include <map>

#include "native_client/src/trusted/service_runtime/filename_util.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"

#include "gtest/gtest.h"

using std::string;
using std::map;
using std::pair;
using std::make_pair;

class FilenameUtilTest : public testing::Test {
};

TEST_F(FilenameUtilTest, TestStartsWith) {
  using nacl_filename_util::StartsWith;
  ASSERT_TRUE(StartsWith("abcd", ""));
  ASSERT_TRUE(StartsWith("abcd", "ab"));
  ASSERT_TRUE(StartsWith("abcd", "abcd"));
  ASSERT_FALSE(StartsWith("abcd", "abcde"));
  ASSERT_FALSE(StartsWith("abcd", "bc"));
  ASSERT_TRUE(StartsWith("", ""));
  ASSERT_FALSE(StartsWith("", "a"));

  ASSERT_TRUE(StartsWith("abcd", 'a'));
  ASSERT_FALSE(StartsWith("abcd", 'c'));
  ASSERT_FALSE(StartsWith("abcd", '\0'));
  ASSERT_FALSE(StartsWith("", '\0'));
  ASSERT_FALSE(StartsWith("1", '\0'));
}

TEST_F(FilenameUtilTest, TestEndsWith) {
  using nacl_filename_util::EndsWith;
  ASSERT_TRUE(EndsWith("abcd", ""));
  ASSERT_TRUE(EndsWith("abcd", "cd"));
  ASSERT_TRUE(EndsWith("abcd", "abcd"));
  ASSERT_FALSE(EndsWith("abcd", "xabcd"));
  ASSERT_FALSE(EndsWith("abcd", "bc"));
  ASSERT_TRUE(EndsWith("", ""));
  ASSERT_FALSE(EndsWith("", "a"));

  ASSERT_TRUE(EndsWith("abcd", 'd'));
  ASSERT_FALSE(EndsWith("abcd", 'c'));
  ASSERT_FALSE(EndsWith("abcd", '\0'));
  ASSERT_FALSE(EndsWith("", '\0'));
  ASSERT_FALSE(EndsWith("1", '\0'));
}

TEST_F(FilenameUtilTest, TestIsAbsolute) {
  using nacl_filename_util::IsAbsolute;
  ASSERT_FALSE(IsAbsolute(""));
  ASSERT_FALSE(IsAbsolute("."));
  ASSERT_FALSE(IsAbsolute("xyz/"));
  ASSERT_FALSE(IsAbsolute("xyz/qwer"));
  ASSERT_TRUE(IsAbsolute("/"));
  ASSERT_TRUE(IsAbsolute("//"));
  ASSERT_TRUE(IsAbsolute("/xyz"));
  ASSERT_TRUE(IsAbsolute("/xyz/qwer/"));
}

TEST_F(FilenameUtilTest, TestStartsWithPath) {
  using nacl_filename_util::StartsWithPath;
  ASSERT_TRUE(StartsWithPath("/foo/bar", "/"));
  ASSERT_TRUE(StartsWithPath("/foo/bar", "/foo"));
  ASSERT_TRUE(StartsWithPath("/foo/bar", "/foo/"));
  ASSERT_TRUE(StartsWithPath("/foo/bar", "/foo/bar"));
  ASSERT_FALSE(StartsWithPath("/foo/bar", "/f"));
  ASSERT_FALSE(StartsWithPath("/foo/bar", "/foo/ba"));
  ASSERT_FALSE(StartsWithPath("/foo/bar", "/foo/bar/"));
  ASSERT_FALSE(StartsWithPath("/foo/bar", "foo/bar"));

  ASSERT_TRUE(StartsWithPath("foo/bar/", "foo"));
  ASSERT_TRUE(StartsWithPath("foo/bar/", "foo/"));
  ASSERT_TRUE(StartsWithPath("foo/bar/", "foo/bar"));
  ASSERT_TRUE(StartsWithPath("foo/bar/", "foo/bar/"));
  ASSERT_FALSE(StartsWithPath("foo/bar/", "fo"));
  ASSERT_FALSE(StartsWithPath("foo/bar/", "fo/ba"));
  ASSERT_FALSE(StartsWithPath("foo/bar/", "foo/bar//"));
}

namespace {
  typedef pair<bool, string> ReplRet;

  ReplRet ReplacePathPrefix(string path, string prefix, string repl) {
    bool ret = nacl_filename_util::ReplacePathPrefix(&path, prefix, repl);
    return ReplRet(ret, path);
  }
}


void assertReplacePathPrefix(string path, string prefix, string repl,
                             bool expected_return, string expected_result) {
  using nacl_filename_util::ReplacePathPrefix;
  bool ret = ReplacePathPrefix(&path, prefix, repl);
  ASSERT_EQ(ret, expected_return);
  ASSERT_EQ(path, expected_result);
}

TEST_F(FilenameUtilTest, TestReplacePathPrefix) {
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/foo/", "/FOO"),      ReplRet(true, "/FOO/bar"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/foo/", "/FOO/"),     ReplRet(true, "/FOO/bar"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/foo",  "/FOO"),      ReplRet(true, "/FOO/bar"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/foo",  "/FOO/"),     ReplRet(true, "/FOO/bar"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/foo/bar", "/FOO"), ReplRet(true, "/FOO"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/foo/bar", "/"),    ReplRet(true, "/"));

  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/f",        "/FOO"), ReplRet(false, "/foo/bar"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/foo/bar/", "/FOO"), ReplRet(false, "/foo/bar"));

  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/",  "/FOO/BAR"), ReplRet(true, "/FOO/BAR/foo/bar"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/",  "FOO/BAR/"), ReplRet(true, "FOO/BAR/foo/bar"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/",    ""),       ReplRet(true, "foo/bar"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "/",    "."),      ReplRet(true, "./foo/bar"));
  ASSERT_EQ(ReplacePathPrefix("foo/bar",  "foo",  "/"),      ReplRet(true, "/bar"));
  ASSERT_EQ(ReplacePathPrefix("foo/bar",  "foo",  "."),      ReplRet(true, "./bar"));
  ASSERT_EQ(ReplacePathPrefix("foo/bar",  "foo/", "."),      ReplRet(true, "./bar"));
  ASSERT_EQ(ReplacePathPrefix("/",        "/",    "/asdf"),  ReplRet(true, "/asdf"));
  ASSERT_EQ(ReplacePathPrefix("",         "/",    "/"),      ReplRet(false, ""));
  ASSERT_EQ(ReplacePathPrefix("/asd/f",  "/asdf", "/"),      ReplRet(false, "/asd/f"));
  ASSERT_EQ(ReplacePathPrefix("/asdf/x", "/asdf", "/"),      ReplRet(true, "/x"));
  ASSERT_EQ(ReplacePathPrefix("/asdf/",  "/asdf", "/"),      ReplRet(true, "/"));
  ASSERT_EQ(ReplacePathPrefix("/asdf",   "/asdf", "/"),      ReplRet(true, "/"));
  ASSERT_EQ(ReplacePathPrefix("/foo/bar", "",     "/"),      ReplRet(true, "/foo/bar"));
  ASSERT_EQ(ReplacePathPrefix("foo/bar",  "",     "/"),      ReplRet(true, "/foo/bar"));
}

TEST_F(FilenameUtilTest, TestAppendComponent) {
  using nacl_filename_util::AppendComponent;
  string s = "/abc/def/";
  AppendComponent(&s, "/bar");
  ASSERT_EQ(s, "/abc/def/bar");
  // Mostly AppendComponent is tested via JoinComponents.
}

TEST_F(FilenameUtilTest, TestJoinComponents) {
  using nacl_filename_util::JoinComponents;

  ASSERT_EQ(JoinComponents("",          ""),           "");
  ASSERT_EQ(JoinComponents("",          "/"),          "/");
  ASSERT_EQ(JoinComponents("",          "abc"),        "abc");
  ASSERT_EQ(JoinComponents("",          "abc/def"),    "abc/def");
  ASSERT_EQ(JoinComponents("",          "/abc/def"),   "/abc/def");
  ASSERT_EQ(JoinComponents("/",         ""),           "/");
  ASSERT_EQ(JoinComponents("abc",       ""),           "abc");
  ASSERT_EQ(JoinComponents("abc/def/",  ""),           "abc/def/");
  ASSERT_EQ(JoinComponents("/abc/def",  ""),           "/abc/def");
  ASSERT_EQ(JoinComponents("/abc",      "def"),        "/abc/def");

  ASSERT_EQ(JoinComponents("abc",       "def"),        "abc/def");
  ASSERT_EQ(JoinComponents("abc",       "/def"),       "abc/def");
  ASSERT_EQ(JoinComponents("abc/",      "/def"),       "abc/def");
  ASSERT_EQ(JoinComponents("abc/",      "def"),        "abc/def");

  ASSERT_EQ(JoinComponents("a",         "b/c/d"),      "a/b/c/d");
  ASSERT_EQ(JoinComponents("a/b/c",     "d"),          "a/b/c/d");
  ASSERT_EQ(JoinComponents("a/",        "b/c/d"),      "a/b/c/d");
  ASSERT_EQ(JoinComponents("a/",        "/b/c/d"),     "a/b/c/d");
  ASSERT_EQ(JoinComponents("a",         "/b/c/d"),     "a/b/c/d");
  ASSERT_EQ(JoinComponents("a/b/c",     "d"),          "a/b/c/d");
}

void assertRemoveFirstComponent(string path, string expectedHead, string expectedTail) {
  using nacl_filename_util::RemoveFirstComponent;
  string head = RemoveFirstComponent(&path);
  ASSERT_EQ(head, expectedHead);
  ASSERT_EQ(path, expectedTail);
}

TEST_F(FilenameUtilTest, TestRemoveFirstComponent) {
  assertRemoveFirstComponent("abc",       "abc", "");
  assertRemoveFirstComponent("abc/def",   "abc", "def");
  assertRemoveFirstComponent("abc//def",  "abc", "/def");
  assertRemoveFirstComponent("a/b/c/d",   "a",   "b/c/d");
  assertRemoveFirstComponent("/abc/def",  "",    "abc/def");
  assertRemoveFirstComponent("//abc/def", "",    "/abc/def");
  assertRemoveFirstComponent("//",        "",    "/");
  assertRemoveFirstComponent("/",         "",    "");
  assertRemoveFirstComponent("",          "",    "");
}

void assertRemoveLastComponent(string path, string expectedHead, string expectedTail) {
  using nacl_filename_util::RemoveLastComponent;
  string tail = RemoveLastComponent(&path);
  ASSERT_EQ(path, expectedHead);
  ASSERT_EQ(tail, expectedTail);
}

TEST_F(FilenameUtilTest, TestRemoveLastComponent) {
  assertRemoveLastComponent("abc",       "",          "abc");
  assertRemoveLastComponent("abc/def",   "abc",       "def");
  assertRemoveLastComponent("abc//def",  "abc/",      "def");
  assertRemoveLastComponent("a/b/c/d",   "a/b/c",     "d");
  assertRemoveLastComponent("/abc/def",  "/abc",      "def");
  assertRemoveLastComponent("/abc/def/", "/abc/def",  "");
  assertRemoveLastComponent("abc/def//", "abc/def/",  "");
  assertRemoveLastComponent("//",        "/",         "");
  assertRemoveLastComponent("/",         "",          "");
  assertRemoveLastComponent("",          "",          "");
}

namespace {
  using nacl_filename_util::FS;

  // A mock filesystem implementation for testing.
  // Current working directory can be set by setting mockFS.cwd directly.
  // Symlinks are added by adding strings to the symlinks map.
  // Paths that contain "N" are considered nonexistent, those with "X" produces
  // EACCES, everything else is assumed to exist.
  class MockFS : public FS {
    public:
      string cwd;
      map<string, string> symlinks;

      virtual int32_t Getcwd(string *path) const {
        *path = cwd;
        return 0;
      }

      virtual int32_t Readlink(const string &path, string *link_path) const {
        if (path.find("N") != string::npos) {
          return -NACL_ABI_ENOENT;
        }
        if (path.find("X") != string::npos) {
          return -NACL_ABI_EACCES;
        }
        map<string, string>::const_iterator it = symlinks.find(path);
        if (it == symlinks.end()) {
          return -NACL_ABI_EINVAL;
        } else {
          *link_path = it->second;
          return 0;
        }
      }
  };

  typedef pair<int32_t, string> PathRet;

  PathRet AbsPath(const FS &fs, string path) {
    string result;
    int32_t retval = nacl_filename_util::AbsPath(fs, path, &result);
    return PathRet(retval, result);
  }

  PathRet RealPath(const FS &fs, string path) {
    string result;
    int32_t retval = nacl_filename_util::RealPath(fs, path, &result);
    return PathRet(retval, result);
  }
}


TEST_F(FilenameUtilTest, TestAbsPath) {
  MockFS fs;
  fs.cwd = "/";
  ASSERT_EQ(AbsPath(fs, "/foo/bar/baz"),          PathRet(0, "/foo/bar/baz"));
  ASSERT_EQ(AbsPath(fs, "foo/bar/baz"),           PathRet(0, "/foo/bar/baz"));
  ASSERT_EQ(AbsPath(fs, "foo/..//./bar/./baz/"),  PathRet(0, "/bar/baz"));
  ASSERT_EQ(AbsPath(fs, ""),                      PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "///"),                   PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "."),                     PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "../.."),                 PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "../..//"),               PathRet(0, "/"));

  // Existence of files doesn't affect AbsPath.
  ASSERT_EQ(AbsPath(fs, "fooN/barN/bazN"),        PathRet(0, "/fooN/barN/bazN"));

  fs.cwd = "/hello/world";
  ASSERT_EQ(AbsPath(fs, "/foo/bar/baz"),          PathRet(0, "/foo/bar/baz"));
  ASSERT_EQ(AbsPath(fs, "foo/bar/baz"),           PathRet(0, "/hello/world/foo/bar/baz"));
  ASSERT_EQ(AbsPath(fs, "foo/..//./bar/./baz/"),  PathRet(0, "/hello/world/bar/baz"));
  ASSERT_EQ(AbsPath(fs, ""),                      PathRet(0, "/hello/world"));
  ASSERT_EQ(AbsPath(fs, "///"),                   PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "."),                     PathRet(0, "/hello/world"));
  ASSERT_EQ(AbsPath(fs, ".."),                    PathRet(0, "/hello"));
  ASSERT_EQ(AbsPath(fs, "../"),                   PathRet(0, "/hello"));
  ASSERT_EQ(AbsPath(fs, "/.."),                   PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "../.."),                 PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "../..//"),               PathRet(0, "/"));

  // Symlinks don't matter for AbsPath.
  fs.symlinks["/foo"] = "/hello";
  ASSERT_EQ(AbsPath(fs, "/foo/bar/baz/"),         PathRet(0, "/foo/bar/baz"));

  // Test cases from earlier tests for CheckCanonical.
  ASSERT_EQ(AbsPath(fs, "/foo"),                  PathRet(0, "/foo"));
  ASSERT_EQ(AbsPath(fs, "/foo/"),                 PathRet(0, "/foo"));
  ASSERT_EQ(AbsPath(fs, "/foo/."),                PathRet(0, "/foo"));
  ASSERT_EQ(AbsPath(fs, "/foo/bar"),              PathRet(0, "/foo/bar"));
  ASSERT_EQ(AbsPath(fs, "//."),                   PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "///////"),               PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "//././/.////.///.././"), PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "/../foo"),               PathRet(0, "/foo"));
  ASSERT_EQ(AbsPath(fs, "/..foo/"),               PathRet(0, "/..foo"));
  ASSERT_EQ(AbsPath(fs, "/.foo/"),                PathRet(0, "/.foo"));
  ASSERT_EQ(AbsPath(fs, "/foo/../bar"),           PathRet(0, "/bar"));
  ASSERT_EQ(AbsPath(fs, "/foo/.."),               PathRet(0, "/"));
  ASSERT_EQ(AbsPath(fs, "/../foo/.././bar/./"),   PathRet(0, "/bar"));
  ASSERT_EQ(AbsPath(fs, "/bar/foo/.."),           PathRet(0, "/bar"));
  ASSERT_EQ(AbsPath(fs, "/..foo/bar/.."),         PathRet(0, "/..foo"));
  ASSERT_EQ(AbsPath(fs, "/foo/../bar/.."),        PathRet(0, "/"));
}

TEST_F(FilenameUtilTest, TestRealPath) {
  // RealPath differs from AbsPath in that it also resolves symlinks.
  MockFS fs;
  fs.cwd = "/usr/var";
  fs.symlinks["/tmp"] = "/var/tmp";
  fs.symlinks["/var"] = "/usr/var";
  fs.symlinks["/tmp/test"] = "NEVER_USED";
  fs.symlinks["/usr/var/link_here"] = "here";
  fs.symlinks["/usr/var/link_here2"] = "./here";
  fs.symlinks["/usr/var/link_up"] = "../";
  fs.symlinks["/usr/var/link_up_up"] = "../../hello";
  fs.symlinks["/usr/var/link_abs"] = "/tmp/abs";
  fs.symlinks["/usr/var/link_rel"] = "test/./rel///";

  // Symlink-less tests.
  ASSERT_EQ(RealPath(fs, "/foo/bar/baz"),         PathRet(0, "/foo/bar/baz"));
  ASSERT_EQ(RealPath(fs, "foo/bar/baz"),          PathRet(0, "/usr/var/foo/bar/baz"));
  ASSERT_EQ(RealPath(fs, "foo/..//./bar/./baz/"), PathRet(0, "/usr/var/bar/baz"));
  ASSERT_EQ(RealPath(fs, ""),                     PathRet(0, "/usr/var"));
  ASSERT_EQ(RealPath(fs, "///"),                  PathRet(0, "/"));
  ASSERT_EQ(RealPath(fs, "."),                    PathRet(0, "/usr/var"));
  ASSERT_EQ(RealPath(fs, ".."),                   PathRet(0, "/usr"));
  ASSERT_EQ(RealPath(fs, "../"),                  PathRet(0, "/usr"));
  ASSERT_EQ(RealPath(fs, "/.."),                  PathRet(0, "/"));
  ASSERT_EQ(RealPath(fs, "../.."),                PathRet(0, "/"));
  ASSERT_EQ(RealPath(fs, "../..//"),              PathRet(0, "/"));

  ASSERT_EQ(RealPath(fs, "/tmp"),                 PathRet(0, "/usr/var/tmp"));
  ASSERT_EQ(RealPath(fs, "/./tmp//./"),           PathRet(0, "/usr/var/tmp"));
  ASSERT_EQ(RealPath(fs, "/var/tmp"),             PathRet(0, "/usr/var/tmp"));
  ASSERT_EQ(RealPath(fs, "/usr/var/tmp"),         PathRet(0, "/usr/var/tmp"));

  // Test handling of symlinks.
  ASSERT_EQ(RealPath(fs, "/var/link_here"),       PathRet(0, "/usr/var/here"));
  ASSERT_EQ(RealPath(fs, "/var/link_here//test"), PathRet(0, "/usr/var/here/test"));
  ASSERT_EQ(RealPath(fs, "/var/link_here2/"),     PathRet(0, "/usr/var/here"));
  ASSERT_EQ(RealPath(fs, "/var/link_up"),         PathRet(0, "/usr"));
  ASSERT_EQ(RealPath(fs, "/var/link_up/test/"),   PathRet(0, "/usr/test"));
  ASSERT_EQ(RealPath(fs, "/var/link_up_up"),      PathRet(0, "/hello"));
  ASSERT_EQ(RealPath(fs, "/var/link_up_up/test/"),PathRet(0, "/hello/test"));
  ASSERT_EQ(RealPath(fs, "/var/link_abs"),        PathRet(0, "/usr/var/tmp/abs"));
  ASSERT_EQ(RealPath(fs, "/var/link_abs/test/"),  PathRet(0, "/usr/var/tmp/abs/test"));
  ASSERT_EQ(RealPath(fs, "/var/link_rel"),        PathRet(0, "/usr/var/test/rel"));
  ASSERT_EQ(RealPath(fs, "/var/link_rel/test/"),  PathRet(0, "/usr/var/test/rel/test"));

  // Same tests, but relative to the current directory.
  ASSERT_EQ(RealPath(fs, "link_here"),            PathRet(0, "/usr/var/here"));
  ASSERT_EQ(RealPath(fs, "link_here//test"),      PathRet(0, "/usr/var/here/test"));
  ASSERT_EQ(RealPath(fs, "link_here2/"),          PathRet(0, "/usr/var/here"));
  ASSERT_EQ(RealPath(fs, "link_up"),              PathRet(0, "/usr"));
  ASSERT_EQ(RealPath(fs, "link_up/test/"),        PathRet(0, "/usr/test"));
  ASSERT_EQ(RealPath(fs, "link_up_up"),           PathRet(0, "/hello"));
  ASSERT_EQ(RealPath(fs, "link_up_up/test/"),     PathRet(0, "/hello/test"));
  ASSERT_EQ(RealPath(fs, "link_abs"),             PathRet(0, "/usr/var/tmp/abs"));
  ASSERT_EQ(RealPath(fs, "link_abs/test/"),       PathRet(0, "/usr/var/tmp/abs/test"));
  ASSERT_EQ(RealPath(fs, "link_rel"),             PathRet(0, "/usr/var/test/rel"));
  ASSERT_EQ(RealPath(fs, "link_rel/test/"),       PathRet(0, "/usr/var/test/rel/test"));

  // Test handling of nonexistent paths.
  ASSERT_EQ(RealPath(fs, "/var/link_hereN"),      PathRet(0, "/usr/var/link_hereN"));
  ASSERT_EQ(RealPath(fs, "/var/fooN/test"),       PathRet(0, "/usr/var/fooN/test"));
  ASSERT_EQ(RealPath(fs, "/fooN/test"),           PathRet(0, "/fooN/test"));
  ASSERT_EQ(RealPath(fs, "link_abs/N/"),          PathRet(0, "/usr/var/tmp/abs/N"));
  ASSERT_EQ(RealPath(fs, "link_abs/N/bar"),       PathRet(0, "/usr/var/tmp/abs/N/bar"));

  // Test handling of paths with errors.
  ASSERT_EQ(RealPath(fs, "/var/link_hereX"),      PathRet(-NACL_ABI_EACCES, ""));
  ASSERT_EQ(RealPath(fs, "/var/fooX/test"),       PathRet(-NACL_ABI_EACCES, ""));
  ASSERT_EQ(RealPath(fs, "/fooX/test"),           PathRet(-NACL_ABI_EACCES, ""));
  ASSERT_EQ(RealPath(fs, "link_abs/X/"),          PathRet(-NACL_ABI_EACCES, ""));
  ASSERT_EQ(RealPath(fs, "link_abs/X/bar"),       PathRet(-NACL_ABI_EACCES, ""));

  // The danger is in leaving /danger after the translation.
  fs.symlinks["/danger"] = "/etc/password";
  fs.symlinks["/dangerX"] = "/etc/password";
  fs.symlinks["/foo/danger"] = "/etc/password";
  fs.symlinks["/fooX/danger"] = "/etc/password";
  ASSERT_EQ(RealPath(fs, "/danger"),              PathRet(0, "/etc/password"));
  ASSERT_EQ(RealPath(fs, "/dangerX"),             PathRet(-NACL_ABI_EACCES, ""));
  ASSERT_EQ(RealPath(fs, "/foo/danger"),          PathRet(0, "/etc/password"));
  ASSERT_EQ(RealPath(fs, "/fooX/danger"),         PathRet(-NACL_ABI_EACCES, ""));
}
