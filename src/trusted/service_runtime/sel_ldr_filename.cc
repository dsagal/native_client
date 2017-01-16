/*
 * Copyright (c) 2016 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "native_client/src/trusted/service_runtime/sel_ldr_filename.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "native_client/src/include/build_config.h"
#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/src/trusted/service_runtime/filename_util.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"
#include "native_client/src/trusted/service_runtime/nacl_copy.h"
#include "native_client/src/trusted/service_runtime/nacl_syscall_common.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"

struct VirtualMount {
  string host_path;
  string virt_path;
  bool is_writable;
};

// This is stored sorted by the decreasing length of virt_path. This ensures
// that translations work by matching the longest virtual prefix first. Note
// that the same order is appropriate for translations from host paths.
vector<VirtualMount> virtual_mounts;


// Note that when storing paths, we store them as absolute normalized paths, to
// make sure we match virtual paths when processing the result of RealPath, and
// match host paths returned by NaClHostDescGetcwd().
int NaClAddMount(const char *mount_spec) {
  string spec(mount_spec);

  // A bare path is equivalent to mounting rw as root. It is mainly supported
  // for compatibility with the way -m worked previously, and for convenience.
  if (spec.rfind(":") == string::npos) {
    spec.append(":/:rw");
  }

  // Parse the "<host-dir>:<virt-dir>:[ro|rw]" spec.
  size_t colon2 = spec.rfind(":");
  if (colon2 == string::npos || colon2 == 0) {
    NaClLog(LOG_ERROR, "NaClAddMount: Invalid -m mount spec");
    return 0;
  }
  string options = spec.substr(colon2 + 1);
  if (options != "ro" && options != "rw") {
    NaClLog(LOG_ERROR, "NaClAddMount: -m mount option must be 'ro' or 'rw'");
    return 0;
  }

  size_t colon1 = spec.rfind(":", colon2 - 1);
  if (colon1 == string::npos || colon1 == 0) {
    NaClLog(LOG_ERROR, "NaClAddMount: Invalid -m mount spec");
    return 0;
  }
  string virt_path = spec.substr(colon1 + 1, colon2 - colon1 - 1);

  if (!IsAbsolute(virt_path)) {
    NaClLog(LOG_ERROR, "NaClAddMount: -m mount path must be absolute");
    return 0;
  }
  // Calling AbsPath() normalizes the path, ensuring it contains no . .. or //.
  virt_path = AbsPath(virt_path);

  string host_path = spec.substr(0, colon1);

  // It is also important to normalize the host path. Since it may use a
  // different notion of separator and absolute path (e.g. on Windows), we
  // achieve it by chdir() + getcwd(). That also ensures the mapped directory
  // is in fact a directory.
  char cwd_orig[MAXPATHLEN] = "";
  char abs_host[MAXPATHLEN] = "";
  if (0 != NaClHostDescGetcwd(cwd_orig, sizeof cwd_orig) ||
      0 != NaClHostDescChdir(host_path.c_str()) ||
      0 != NaClHostDescGetcwd(abs_host, sizeof abs_host) ||
      0 != NaClHostDescChdir(cwd_orig)) {
    NaClLog(LOG_ERROR, "NaClAddMount: error testing -m host directory");
    return 0;
  }

  struct VirtualMount mount;
  mount.virt_path = virt_path;
  mount.host_path = abs_host;
  mount.is_writable = writable;

  // Find the insert position, sorted by decreasing length of virt_path.
  vector<VirtualMount>::iterator it;
  for (it = virtual_mounts.begin(); it != virtual_mounts.end(); ++it) {
    if (it->virt_path.length() < mount.virt_path.length()) {
      break;
    }
  }
  virtual_mounts.insert(it, mount);
  return 1;
}

int NaClMountsEnabled() {
  return !virtual_mounts.empty() ? 1 : 0;
}

namespace {

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
bool TranslatePath(const string &src_path, string *dest_path,
                   bool to_host, bool *writable) {
  string ret = host_path;
  vector<VirtualMount>::const_iterator it;
  for (it = virtual_mounts.begin(); it != virtual_mounts.end(); ++it) {
    const string &from = to_host ? it->virt_path : it->host_path;
    const string &to = to_host ? it->host_path : it->virt_path;
    if (ReplacePathPrefix(ret, from, to)) {
      if (writable) {
        *writable = it->is_writable;
      }
      return true;
    }
  }
  return false;
}


/*
 * Transforms a raw file path from the user into an absolute path prefixed by
 * the mounted file system root (or leave it as a relative path). Also validates
 * the path to ensure it does not access anything outside the mount point.
 *
 * @param[in/out] dest The raw file path from the user.
 * @param[in] dest_max_size The size of the buffer holding dest.
 * @return 0 on success, else a NaCl errno.
 */
uint32_t CopyHostPathMounted(char *dest, size_t dest_max_size,
                             bool *is_writable) {
  /* Transform the input C string into a std::string for easier manipulation. */
  const std::string raw_path(dest);

  if (raw_path.empty()) {
    NaClLog(LOG_ERROR, "Dest cannot be empty path\n");
    return -NACL_ABI_ENOENT;
  }

  CHECK(dest_max_size == NACL_CONFIG_PATH_MAX);
  CHECK(raw_path.length() < NACL_CONFIG_PATH_MAX);

  std::string resolved_path, host_path;
  if (!RealPath(raw_path, &resolved_path)) {
    return -NaClXlateErrno(errno);
  }
  if (!TranslatePath(resolved_path, &host_path, true, is_writable)) {
    return -NACL_ABI_EACCES;
  }

  if (host_path.length() + 1 > dest_max_size) {
    NaClLog(LOG_WARNING, "Pathname too long: %s\n", host_path.c_str());
    return -NACL_ABI_ENAMETOOLONG;
  }
  /* Copy the C++ string into its C string destination. */
  strcpy(dest, host_path.c_str()); //NOLINT
  return 0;
}

}  // namespace


uint32_t CopyHostPathInFromUser(struct NaClApp *nap,
                                char           *dest,
                                size_t         dest_max_size,
                                uint32_t       src,
                                uint32_t       req_writable) {
  /*
   * NaClCopyInFromUserZStr may (try to) get bytes that is outside the
   * app's address space and generate a fault.
   */
  if (!NaClCopyInFromUserZStr(nap, dest, dest_max_size, src)) {
    if (dest[0] == '\0') {
      NaClLog(LOG_ERROR, "NaClSys: invalid address for pathname\n");
      return (uint32_t) -NACL_ABI_EFAULT;
    }

    NaClLog(LOG_ERROR, "NaClSys: pathname string too long\n");
    return (uint32_t) -NACL_ABI_ENAMETOOLONG;
  }

  /*
   * Without the '-m' option, this function should act like a simple
   * raw path copy.
   */
  if (NaClAclBypassChecks) {
    return 0;
  }
  bool is_writable = false;
  uint32_t retval = CopyHostPathMounted(dest, dest_max_size, &is_writable);
  if (retval == 0 && req_writable && !is_writable) {
    retval = -NACL_ABI_ACCESS;
  }
  return retval;
}


uint32_t TranslateVirtualPath(const char *src_path, char *dest_path,
                              size_t dest_max_size, int to_host) {
  string dest;

  if (NaClAclBypassChecks) {
    dest = src_path;
  } else {
    if (!TranslatePath(src_path, &dest, to_host, NULL)) {
      return -NACL_ABI_EACCES;
    }
  }

  if (dest.length() + 1 > dest_max_size) {
    return -NACL_ABI_ENAMETOOLONG;
  }
  /* Copy the C++ string into its C string destination. */
  strcpy(dest_path, dest.c_str()); //NOLINT
  return 0;
}
