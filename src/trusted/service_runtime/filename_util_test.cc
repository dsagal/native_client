/*
 * Copyright (c) 2016 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string>
#include <vector>

#include "native_client/src/trusted/service_runtime/filename_util.h"

#include "gtest/gtest.h"

using std::string;

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


void assertReplacePathPrefix(string path, string prefix, string repl,
                             bool expected_return, string expected_result) {
  using nacl_filename_util::ReplacePathPrefix;
  bool ret = ReplacePathPrefix(&path, prefix, repl);
  ASSERT_EQ(ret, expected_return);
  ASSERT_EQ(path, expected_result);
}

TEST_F(FilenameUtilTest, TestReplacePathPrefix) {
  // TODO Add more cases.
  assertReplacePathPrefix("/foo/bar", "/foo/", "/FOO",  true, "/FOO/bar");
  assertReplacePathPrefix("/foo/bar", "/foo/", "/FOO/", true, "/FOO/bar");
  assertReplacePathPrefix("/foo/bar", "/foo",  "/FOO",  true, "/FOO/bar");
  assertReplacePathPrefix("/foo/bar", "/foo",  "/FOO/", true, "/FOO/bar");

  assertReplacePathPrefix("/foo/bar", "/f",         "/FOO",  false, "/foo/bar");
  assertReplacePathPrefix("/foo/bar", "/foo/bar/",  "/FOO",  false, "/foo/bar");
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

/*
void CheckCanonical(const std::string &abs_path, const std::string &goal_path,
                    const std::vector<std::string> &goal_subpaths) {
  std::string real_path;
  std::vector<std::string> required_subpaths;
  CanonicalizeAbsolutePath(abs_path, &real_path, &required_subpaths);
  ASSERT_STREQ(goal_path.c_str(), real_path.c_str());
  ASSERT_EQ(goal_subpaths.size(), required_subpaths.size());
  for (size_t i = 0; i != goal_subpaths.size(); i++) {
    ASSERT_STREQ(goal_subpaths[i].c_str(), required_subpaths[i].c_str());
  }
}

TEST_F(SelLdrFilesTest, TestModifiedRoot) {
  std::vector<std::string> goal_subpaths;
  goal_subpaths.clear();
  CheckCanonical("/foo", "/foo", goal_subpaths);
  CheckCanonical("/foo/", "/foo/", goal_subpaths);
  CheckCanonical("/foo/.", "/foo/", goal_subpaths);
  CheckCanonical("/foo/bar", "/foo/bar", goal_subpaths);

  CheckCanonical("//.", "/", goal_subpaths);
  CheckCanonical("///////", "/", goal_subpaths);
  CheckCanonical("//././/.////.///.././", "/", goal_subpaths);

  CheckCanonical("/../foo", "/foo", goal_subpaths);
  CheckCanonical("/..foo/", "/..foo/", goal_subpaths);
  CheckCanonical("/.foo/", "/.foo/", goal_subpaths);

  std::string tmp_foo[] = {"/foo/"};
  goal_subpaths.assign(tmp_foo, tmp_foo + 1);
  CheckCanonical("/foo/../bar", "/bar", goal_subpaths);
  CheckCanonical("/foo/..", "/", goal_subpaths);
  CheckCanonical("/../foo/.././bar/./", "/bar/", goal_subpaths);

  std::string tmp_bar_foo[] = {"/bar/foo/"};
  goal_subpaths.assign(tmp_bar_foo, tmp_bar_foo + 1);
  CheckCanonical("/bar/foo/..", "/bar", goal_subpaths);

  std::string tmp_foo_bar[] = {"/..foo/bar/"};
  goal_subpaths.assign(tmp_foo_bar, tmp_foo_bar + 1);
  CheckCanonical("/..foo/bar/..", "/..foo", goal_subpaths);

  std::string tmp_foo_and_bar[] = {"/foo/", "/bar/"};
  goal_subpaths.assign(tmp_foo_and_bar, tmp_foo_and_bar + 2);
  CheckCanonical("/foo/../bar/..", "/", goal_subpaths);
}
*/
