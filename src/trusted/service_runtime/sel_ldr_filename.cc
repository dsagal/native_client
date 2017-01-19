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

using std::vector;
using std::string;
using nacl_filename_util::IsAbsolute;
using nacl_filename_util::AbsPath;
using nacl_filename_util::RealPath;
using nacl_filename_util::SandboxFS;
using nacl_filename_util::CreateSandboxFS;

namespace {
  SandboxFS *sandbox_fs = NULL;
}


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
    NaClLog(LOG_ERROR, "NaClAddMount: Invalid -m mount spec\n");
    return 0;
  }
  string options = spec.substr(colon2 + 1);
  if (options != "ro" && options != "rw") {
    NaClLog(LOG_ERROR, "NaClAddMount: -m mount option must be 'ro' or 'rw'\n");
    return 0;
  }

  size_t colon1 = spec.rfind(":", colon2 - 1);
  if (colon1 == string::npos || colon1 == 0) {
    NaClLog(LOG_ERROR, "NaClAddMount: Invalid -m mount spec\n");
    return 0;
  }
  string virt_path = spec.substr(colon1 + 1, colon2 - colon1 - 1);

  if (!IsAbsolute(virt_path)) {
    NaClLog(LOG_ERROR, "NaClAddMount: -m mount path must be absolute\n");
    return 0;
  }

  // Create the global SandboxFS which we'll use for path translation.
  if (!sandbox_fs) {
    sandbox_fs = CreateSandboxFS();
    if (!sandbox_fs) {
      NaClLog(LOG_ERROR, "NaClAddMount: error creating SandboxFS for -m\n");
      return 0;
    }
  }

  // Calling AbsPath() normalizes the path, ensuring it contains no . .. or //.
  string abs_virt;
  int32_t retval = AbsPath(*sandbox_fs, virt_path, &abs_virt);
  if (retval != 0) {
    NaClLog(LOG_ERROR, "NaClAddMount: error normalizing -m mount path\n");
    return 0;
  }

  string host_path = spec.substr(0, colon1);

  // It is also important to normalize the host path. Since it may use a
  // different notion of separator and absolute path (e.g. on Windows), we
  // achieve it by chdir() + getcwd(). That also ensures the mapped directory
  // is in fact a directory.
  char cwd_orig[NACL_CONFIG_PATH_MAX] = "";
  char abs_host[NACL_CONFIG_PATH_MAX] = "";
  if (0 != NaClHostDescGetcwd(cwd_orig, sizeof cwd_orig) ||
      0 != NaClHostDescChdir(host_path.c_str()) ||
      0 != NaClHostDescGetcwd(abs_host, sizeof abs_host) ||
      0 != NaClHostDescChdir(cwd_orig)) {
    NaClLog(LOG_ERROR, "NaClAddMount: error testing -m host directory\n");
    return 0;
  }

  sandbox_fs->AddMount(abs_host, abs_virt, options == "rw");
  return 1;
}


int NaClMountsEnabled() {
  return sandbox_fs->Enabled() ? 1 : 0;
}


int32_t NaClSandboxGetcwd(char *buf, size_t buf_size) {
  if (sandbox_fs) {
    string cwd;
    int32_t retval = sandbox_fs->Getcwd(&cwd);
    if (retval != 0) {
      return retval;
    }
    if (cwd.length() + 1 > buf_size) {
      return -NACL_ABI_ENAMETOOLONG;
    }
    strncpy(buf, cwd.c_str(), cwd.length() + 1);
    return 0;
  } if (NaClAclBypassChecks) {
    return NaClHostDescGetcwd(buf, buf_size);
  }
  return -NACL_ABI_EACCES;
}

int32_t NaClSandboxChdir(const char *path) {
  if (sandbox_fs) {
    string host_path;
    if (!sandbox_fs->TranslateToHost(path, &host_path, NULL)) {
      return -NACL_ABI_EACCES;
    }
    return NaClHostDescChdir(host_path.c_str());
  } else if (NaClAclBypassChecks) {
    return NaClHostDescChdir(path);
  }
  return -NACL_ABI_EACCES;
}


uint32_t CopyHostPathInFromUser(struct NaClApp *nap,
                                char           *dest,
                                size_t         dest_max_size,
                                uint32_t       src,
                                bool           req_writable,
                                int32_t        link_flag) {
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

  /* Transform the input C string into a std::string for easier manipulation. */
  const string sandbox_path(dest);
  string resolved_path, host_path;

  int32_t retval = RealPath(*sandbox_fs, sandbox_path, &resolved_path,
                            link_flag);
  if (retval == 0) {
    bool is_writable = false;
    if (!sandbox_fs->TranslateToHost(resolved_path, &host_path, &is_writable) ||
        (req_writable && !is_writable)) {

      retval = -NACL_ABI_EACCES;

    } else if (host_path.length() + 1 > dest_max_size) {
      NaClLog(LOG_WARNING, "NaClSys: resolved pathname too long\n");
      retval = -NACL_ABI_ENAMETOOLONG;

    } else {
      /* Copy the C++ string into its C string destination. */
      strncpy(dest, host_path.c_str(), host_path.length() + 1);
      return 0;
    }
  }
  // Make sure we don't leave an unsafe path in *dest, in case any code
  // (wrongly) uses *dest despite us returning an error.
  dest[0] = '\0';
  return retval;
}
