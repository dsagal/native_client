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


/*
 * Makes path absolute and normalizes it, eliminating extra "/" characters and
 * references to /./ and /../, but not resolving symlinks.
 *
 * @param[in] path The path to be resolved (in virtual FS).
 * @param[out] resolved_path The resulting absolute path (in virtual FS).
 * @return true on success, false on failure (and sets errno).
 */
bool AbsPath(const std::string &path, std::string *resolved_path);

/*
 * Resolves path into a canonical absolute path, resolving all symbolic links,
 * extra "/" characters, and references to /./ and /../. It interprets
 * everything within the virtual filesystem, ensuring in particular that no
 * prefix of resolved_path is a symlink in the virtual FS.
 *
 * @param[in] path The path to be resolved (in virtual FS).
 * @param[out] resolved_path The resulting absolute path (in virtual FS).
 * @return true on success, false on failure (and sets errno).
 */
bool RealPath(const std::string &path, std::string *resolved_path);

#endif /* NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_FILENAME_UTIL_H_ */
