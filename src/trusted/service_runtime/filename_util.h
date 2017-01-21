/*
 * Copyright (c) 2016 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_FILENAME_UTIL_H_
#define NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_FILENAME_UTIL_H_

#include <stdlib.h>

#include <string>
#include <vector>

/*
 * TODO: This warning probably belongs elsewhere.
 *
 * NOTE that any user-level implementation of a virtual filesystem, such as
 * this one, suffers from a possible race condition between preparing a safe
 * path and using it. If another thread or process runs between these steps and
 * inserts a symlink into that path, then on use, the host OS will follow the
 * symlink, which may take it outside of the restricted filesystem.
 *
 * For a simple example, before opening /foo, we resolve it to /ROOT/foo, find
 * that it doesn't exist, and call open(/ROOT/foo) on the host side. In
 * between, untrusted code can add a symlink /foo->/etc/passwd, and the
 * host-side open() call will end up opening /etc/passwd.
 *
 * A similar exploit is possible without creating a symlink, by moving an
 * existing symlink, or a directory containing a symlink.
 *
 * Our solution is to prevent any possibility of a new symlink appearing along
 * any path by prohibiting the symlink() call AND prohibiting renames of
 * symlinks or directories.
 *
 * To relax this, one might want to use a lock between translating and
 * verifying a path and using it. This can address the race condition between
 * threads, but it may still be possible for two different untrusted processes
 * to coordinate to do this exploit, since they would not be sharing the lock.
 *
 * Note that trusted code can still create symlinks, but an exploit would
 * require significant coordination between untrusted and trusted code.
 *
 * (An ideal solution would be if the host OS supported a mode of operations
 * where symlinks are completely disallowed; then we could translate to a
 * symlink-free path and use that, and any exploit that inserted symlinks would
 * cause a rejection rather than an escape out of the mounted directory.)
 */

namespace nacl_filename_util {

// Class FS abstracts out the operations required by this module. It also
// allows mocking them for testing.
class FS {
  public:
    virtual ~FS() {}

    // Returns 0 on success, else a negated NaCl errno.
    virtual int32_t Getcwd(std::string *path) const = 0;

    // Returns 0 on success, else a negated NaCl errno. Assumes that path is
    // fully resolved and contains no symlinks. Therefore, it isn't safe to use
    // except by RealPath().
    virtual int32_t RawReadlink(const std::string &path,
                                std::string *link_path) const = 0;
};

bool StartsWith(const std::string &str, const std::string &prefix);
bool StartsWith(const std::string &str, char prefix) ;
bool EndsWith(const std::string &str, const std::string &suffix);
bool EndsWith(const std::string &str, char suffix);
bool IsAbsolute(const std::string &path);
bool StartsWithPath(const std::string &path, const std::string &path_prefix);
bool ReplacePathPrefix(std::string *path, const std::string &prefix,
                       const std::string &repl);
void AppendComponent(std::string *path, const std::string &tail);
std::string JoinComponents(const std::string &head, const std::string &tail);
std::string RemoveLastComponent(std::string *path);
std::string RemoveFirstComponent(std::string *path);

/*
 * Makes path absolute and normalizes it, eliminating extra "/" characters and
 * references to /./ and /../, but not resolving symlinks.
 *
 * @param[in] path The path to be resolved (in virtual FS).
 * @param[out] resolved_path The resulting absolute path (in virtual FS).
 * @return 0 on success, else a negated NaCl errno.
 */
int32_t AbsPath(const FS &fs, const std::string &path,
                std::string *resolved_path);

/*
 * Resolves path into a canonical absolute path, resolving all symbolic links,
 * extra "/" characters, and references to /./ and /../. It interprets
 * everything within the virtual filesystem, ensuring in particular that no
 * prefix of resolved_path is a symlink in the virtual FS.
 *
 * @param[in] path The path to be resolved (in virtual FS).
 * @param[out] resolved_path The resulting absolute path (in virtual FS).
 * @param[in] link_flag Control what to do if the full path is a symlink:
 *      0 to resolve it, >0 to return the path to it, <0 to disallow it,
 *      and return link_flag as the error code in that case.
 * @return 0 on success, else a negated NaCl errno.
 */
int32_t RealPath(const FS &fs, const std::string &path,
                 std::string *resolved_path,
                 int32_t link_flag = 0);



/**
 * Implements the translation of paths between the sandbox and host
 * filesystems, as required for the -m option.
 */
class SandboxFS : public FS {
  public:
    /*
     * The host FS implementation is taken as parameter, to allow test mocks.
     */
    SandboxFS(const FS &host_fs) : host_fs_(host_fs) {}

    /*
     * Return whether any translation is enabled.
     */
    bool Enabled() const {
      return !virtual_mounts_.empty();
    }

    /*
     * Adds a mount point. Returns false on error.
     */
    bool AddMount(std::string host_path, std::string virt_path,
                  bool is_writable);

    /*
     * Translates a sandbox path to a host path, filling in *is_writable flag
     * (when not NULL). Returns true on success, false if there is no mapping.
     */
    bool TranslateToHost(const std::string &virt_path, std::string *host_path,
                         bool *is_writable) const;

    /*
     * Translates a host path to a sandbox path, filling in *is_writable flag
     * (when not NULL). Returns true on success, false if there is no mapping.
     */
    bool TranslateFromHost(std::string host_path, std::string *virt_path,
                           bool *is_writable) const;

    /*
     * Converts a sandbox path into a host path, resolving all symbolic links,
     * extra "/" characters, and references to /./ and /../. It produces a host
     * path for use with underlying host filesystem functions.
     *
     * @param[in] virt_path The path to be resolved (as seen in the sandbox).
     * @param[out] host_path The resulting host path.
     * @param[in] req_writable If non-zero, path must be on a writable mount.
     * @param[in] link_flag Control what to do if the full path is a symlink:
     *      0 to resolve fully, >0 to resolve the path leading up to it, <0 to
     *      disallow it, and return link_flag as the error code in that case.
     * @return 0 on success, else a negated NaCl errno.
     */
    int32_t ResolveToHost(const std::string &virt_path, std::string *host_path,
                          bool req_writable, int32_t link_flag) const;

  private:
    int32_t Getcwd(std::string *path) const;
    int32_t RawReadlink(const std::string &path, std::string *link_path) const;

    bool TranslatePathImpl(const std::string &src_path, std::string *dest_path,
                           bool to_host, bool *writable) const;

    // The underlying host filesystem.
    const FS &host_fs_;

    struct VirtualMount {
      std::string host_path;
      std::string virt_path;
      bool is_writable;
    };

    // These are stored sorted by the decreasing length of virt_path. This
    // ensures that we match the longest virtual prefix first. Note that the
    // same order is appropriate for both directions of translation.
    std::vector<VirtualMount> virtual_mounts_;
};

/*
 * Creates and returns a SandboxFS object to use for translating paths between
 * the sandbox and host filesystems, as required for the -m option.
 */
SandboxFS *CreateSandboxFS();

}

#endif /* NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_FILENAME_UTIL_H_ */
