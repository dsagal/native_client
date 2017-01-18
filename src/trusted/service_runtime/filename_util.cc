/*
 * Copyright (c) 2016 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "native_client/src/trusted/service_runtime/filename_util.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"

#include <string>
#include <vector>

using std::vector;
using std::string;

/*
 * This defines the maximum number of symbolic links that may be
 * expanded in a path name. It should be set high enough to allow all
 * legitimate uses, but halt infinite loops reasonably quickly. It should
 * possibly be defined in nacl_config.h instead, but this is currently the only
 * file that uses it.
 */
#define NACL_FILEUTIL_MAXSYMLINKS     32

namespace nacl_filename_util {

char SEP = '/';
string CURDIR = ".";
string PARDIR = "..";

/*
 * Returns true if str begins with prefix.
 */
bool StartsWith(const string &str, const string &prefix) {
  return str.rfind(prefix, 0) == 0;
}

/*
 * Returns true if str begins with the character prefix.
 */
bool StartsWith(const string &str, char prefix) {
  return !str.empty() && str[0] == prefix;
}

/*
 * Returns true if str ends with suffix.
 */
bool EndsWith(const string &str, const string &suffix) {
  if (str.size() < suffix.size()) { return false; }
  size_t index = str.size() - suffix.size();
  return str.find(suffix, index) == index;
}

/*
 * Returns true if str ends with the character suffix.
 */
bool EndsWith(const string &str, char suffix) {
  return !str.empty() && str[str.size() - 1] == suffix;
}

/*
 * Returns true if path is absolute (starts with /).
 */
bool IsAbsolute(const string &path) {
  return StartsWith(path, SEP);
}

/*
 * Returns whether path starts with path_prefix, considering whole components
 * only.
 *
 * E.g. "/foo/bar" starts with "/foo" or "/foo/", but not with "/f" or
 * "/foo/bar/". Paths may optionally end in a slash, but should otherwise be
 * normalized (e.g. no consecutive slashes).
 */
bool StartsWithPath(const string &path, const string &path_prefix) {
  return StartsWith(path, path_prefix) && (
    path.size() == path_prefix.size() ||
    EndsWith(path_prefix, SEP) ||
    path[path_prefix.size()] == SEP ||
    path_prefix.empty()
  );
}

/*
 * If *path starts with prefix, replaces that prefix with repl and returns
 * true. Otherwise returns false and leaves *path unmodified.
 *
 * E.g. path of "/foo/bar" turns into "/FOO/bar" on replacing "/foo/" or "/foo"
 * with "/FOO" or "/FOO/", but returns false if prefix is "/f" or "/foo/bar/".
 */
bool ReplacePathPrefix(string *path,
                       const string &prefix, const string &repl) {
  if (StartsWithPath(*path, prefix)) {
    path->assign(JoinComponents(repl, path->substr(prefix.size())));
    return true;
  }
  return false;
}

/*
 * Appends tail to path, joining the with a single "/".
 */
void AppendComponent(string *path, const string &tail) {
  if (tail.empty()) {
    return;
  }
  if (path->empty()) {
    path->assign(tail);
  } else {
    if (!EndsWith(*path, SEP)) {
      path->push_back(SEP);
    }
    path->append(tail, StartsWith(tail, SEP) ? 1 : 0, string::npos);
  }
}

/*
 * Returns head joined with tail with a single "/" between them.
 */
string JoinComponents(const string &head, const string &tail) {
  string ret = head;
  AppendComponent(&ret, tail);
  return ret;
}

/*
 * Removes and returns the part of path until the first slash, leaving path to
 * start with the character following the slash.
 */
string RemoveFirstComponent(string *path) {
  size_t pos = path->find(SEP);
  string ret;
  if (pos == string::npos) {
    path->swap(ret);
  } else {
    ret = path->substr(0, pos);
    path->erase(0, pos + 1);
  }
  return ret;
}

/*
 * Removes and returns the part of path after the last slash, leaving path to
 * end with the character preceding the slash.
 */
string RemoveLastComponent(string *path) {
  size_t pos = path->rfind(SEP);
  string ret;
  if (pos == string::npos) {
    path->swap(ret);
  } else {
    ret = path->substr(pos + 1);
    path->erase(pos);
  }
  return ret;
}

/*
 * Implements both RealPath and AbsPath. The only difference is whether
 * it follows symlinks.
 */
int32_t RealPathImpl(const FS &fs, const string &path, string *resolved_path,
                     bool resolve_links) {
  int32_t retval = 0;

  // The invariants below are:
  // - done is an absolute path with all symlinks resolved except possibly the
  //   very last one (i.e. full path).
  // - rest is relative to done (even if it starts with a slash).
  string done, rest = path;
  size_t link_count = 0;

  // Ensure that we start with an absolute path (i.e. add cwd if relative).
  if (!IsAbsolute(rest)) {
    string cwd;
    retval = fs.Getcwd(&cwd);
    if (retval != 0) {
      return retval;
    }
    rest = JoinComponents(cwd, rest);
  }

  done = SEP;
  while (!rest.empty()) {
    string head = RemoveFirstComponent(&rest);
    if (head.empty() || head == CURDIR) {
      // Skip consecutive slashes and "." directories.
    } else if (head == PARDIR) {
      RemoveLastComponent(&done);
      if (done.empty()) {
        done = SEP;
      }
    } else {
      AppendComponent(&done, head);

      if (resolve_links) {
        string link_path;
        retval = fs.Readlink(done, &link_path);
        if (retval == 0) {
          // Protect against infinite links.
          if (++link_count > NACL_FILEUTIL_MAXSYMLINKS) {
            return -NACL_ABI_ELOOP;
          }
          if (IsAbsolute(link_path)) {
            done = SEP;
          } else {
            RemoveLastComponent(&done);
          }
          rest = JoinComponents(link_path, rest);
        } else if (retval == -NACL_ABI_EINVAL || retval == -NACL_ABI_ENOENT) {
          // EINVAL is the common case when the file exists but isn't a symlink.
          // ENOENT is a non-existent path.
        } else {
          // Some actual error reading new_path. We fail here to ensure the
          // resulting path has no symlinks (it would be exploitable to leave a
          // symlink with insufficient permissions, for example).
          return retval;
        }
      }
    }
  }
  *resolved_path = done;
  return 0;
}

int32_t AbsPath(const FS &fs, const string &path, string *resolved_path) {
  return nacl_filename_util::RealPathImpl(fs, path, resolved_path, false);
}

int32_t RealPath(const FS &fs, const string &path, string *resolved_path) {
  return nacl_filename_util::RealPathImpl(fs, path, resolved_path, true);
}

}
