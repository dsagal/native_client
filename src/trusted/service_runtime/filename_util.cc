/*
 * Copyright (c) 2016 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "native_client/src/shared/platform/nacl_host_desc.h"
#include "native_client/src/trusted/service_runtime/filename_util.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"
#include "native_client/src/trusted/service_runtime/nacl_config.h"

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
 * start with first following non-slash character.
 */
string RemoveFirstComponent(string *path) {
  size_t pos = path->find(SEP);
  string ret;
  if (pos == string::npos) {
    path->swap(ret);
  } else {
    ret = path->substr(0, pos);
    path->erase(0, path->find_first_not_of(SEP, pos));
  }
  return ret;
}

/*
 * Removes and returns the part of path after the last slash, leaving path to
 * end with the last preceding non-slash character.
 */
string RemoveLastComponent(string *path) {
  size_t pos = path->rfind(SEP);
  string ret;
  if (pos == string::npos) {
    path->swap(ret);
  } else {
    ret = path->substr(pos + 1);
    path->erase(path->find_last_not_of(SEP, pos) + 1);
  }
  return ret;
}

/*
 * Implements both RealPath and AbsPath. The only difference is whether
 * it follows symlinks.
 */
int32_t RealPathImpl(const FS &fs, const string &path, string *resolved_path,
                     bool resolve_links, int32_t link_flag) {
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
        retval = fs.RawReadlink(done, &link_path);
        if (retval == 0) {
          if (rest.empty()) {
            // If it is the last component, then act according to the link_flag.
            if (link_flag < 0) {
              return link_flag;
            }
            if (link_flag > 0) {
              continue;
            }
          }

          // Protect against infinite links.
          if (++link_count > NACL_FILEUTIL_MAXSYMLINKS) {
            return -NACL_ABI_ELOOP;
          }
          if (IsAbsolute(link_path)) {
            done = SEP;
          } else {
            RemoveLastComponent(&done);
            if (done.empty()) {
              done = SEP;
            }
          }
          rest = JoinComponents(link_path, rest);
        } else if (retval == -NACL_ABI_EINVAL ||
                   (retval == -NACL_ABI_ENOENT && rest.empty())) {
          // EINVAL is the common case when the file exists but isn't a symlink.
          // ENOENT is a non-existent path, OK for the last component only.
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
  return nacl_filename_util::RealPathImpl(fs, path, resolved_path, false, false);
}

int32_t RealPath(const FS &fs, const string &path, string *resolved_path,
                 int32_t link_flag) {
  return nacl_filename_util::RealPathImpl(fs, path, resolved_path, true,
                                          link_flag);
}

/*
 * Implements the FS interface for the host filesystem. This allow using a mock
 * implementation to test SandboxFS methods.
 */
class HostFS : public FS {
  public:
    int32_t Getcwd(string *path) const {
      char buf[NACL_CONFIG_PATH_MAX] = "";
      int32_t retval = NaClHostDescGetcwd(buf, sizeof buf);
      if (retval != 0) {
        return retval;
      }
      path->assign(buf);
      return 0;
    }

    int32_t RawReadlink(const std::string &path, std::string *link_path) const {
      char buf[NACL_CONFIG_PATH_MAX] = "";
      int32_t retval = NaClHostDescReadlink(path.c_str(), buf, sizeof buf);
      if (retval < 0) {
        return retval;
      }
      // A positive value is the number of characters placed into buf (with no
      // terminating null). If it fills the buffer, treat it as truncation.
      if ((uint32_t)retval >= sizeof buf) {
        return -NACL_ABI_ENAMETOOLONG;
      }
      // Note that symlink target is always interpreted as a virtual path, and
      // we do not translate it. Reliable translation isn't trivial because the
      // path may not be normalized. For symlinks that need to work in both
      // host and virtual OS, use relative paths.
      link_path->assign(buf, retval);   // Take the first retval bytes of buf.
      return 0;
    }
};


void SandboxFS::AddMount(string host_path, string virt_path, bool is_writable) {
  VirtualMount mount;
  mount.host_path = host_path;
  mount.virt_path = virt_path;
  mount.is_writable = is_writable;

  // Find the insert position, sorted by decreasing length of virt_path.
  vector<VirtualMount>::iterator it;
  for (it = virtual_mounts_.begin(); it != virtual_mounts_.end(); ++it) {
    if (it->virt_path.length() < mount.virt_path.length()) {
      break;
    }
  }
  virtual_mounts_.insert(it, mount);
}

/*
 * Translates a path between host and virtual filesystems. The direction is
 * determined by the to_host flag.
 *
 * @param[in] src_path The source path (virtual if to_host is set, else host).
 * @param[out] dest_path The output path (host if to_host is set, else virtual).
 * @param[in] to_host True to translate virtual to host, false for backwards.
 * @param[out] writable Will contain whether this path is on a writable mount.
 * @return true on success, false if src_path matched no mount points.
 */
bool SandboxFS::TranslatePathImpl(const string &src_path, string *dest_path,
                                  bool to_host, bool *writable) const {
  string dest(src_path);
  vector<VirtualMount>::const_iterator it;
  for (it = virtual_mounts_.begin(); it != virtual_mounts_.end(); ++it) {
    const string &from = to_host ? it->virt_path : it->host_path;
    const string &to = to_host ? it->host_path : it->virt_path;
    if (ReplacePathPrefix(&dest, from, to)) {
      if (writable) {
        *writable = it->is_writable;
      }
      dest_path->swap(dest);
      return true;
    }
  }
  return false;
}

bool SandboxFS::TranslateToHost(const string &virt_path, string *host_path,
                     bool *is_writable) const {
  return TranslatePathImpl(virt_path, host_path, true, is_writable);
}

bool SandboxFS::TranslateFromHost(const string &host_path, string *virt_path,
                       bool *is_writable) const {
  return TranslatePathImpl(host_path, virt_path, false, is_writable);
}

int32_t SandboxFS::Getcwd(string *path) const {
  string host_path;
  int32_t retval = host_fs_.Getcwd(&host_path);
  if (retval != 0) {
    return retval;
  }
  if (!TranslateFromHost(host_path, path, NULL)) {
    return -NACL_ABI_EACCES;
  }
  return 0;
}

int32_t SandboxFS::RawReadlink(const std::string &path, std::string *link_path) const {
  string host_path;
  if (!TranslateToHost(path, &host_path, NULL)) {
    return -NACL_ABI_EACCES;
  }
  return host_fs_.RawReadlink(host_path, link_path);
}

// Global instance of host_fs.
HostFS host_fs;

SandboxFS *CreateSandboxFS() {
  return new SandboxFS(host_fs);
}

}
