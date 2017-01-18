/*
 * Copyright (c) 2016 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This file contains functions responsible for copying filenames to and from
 * the user process. For restricted filesystem access (refer to
 * documentation/filesystem_access.txt for more details), this abstracts away
 * the details of files mounted at a root directory.
 */

#ifndef NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_SEL_LDR_FILENAME_H_
#define NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_SEL_LDR_FILENAME_H_

#include "native_client/src/include/nacl_base.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"

EXTERN_C_BEGIN

/*
 * Given a mount spec of the form <host-dir>:<virt-dir>:[ro|rw], as expected by
 * the -m option, adds it to internal structures for path translation.
 *
 * @param[in] mount_spec A mount spec of the form * <host-dir>:<virt-dir>:[ro|rw]
 *    (or just <host-dir>, which will be interpreted as '<host-dir>:/:rw').
 * @return 1 on success, 0 on error.
 */
int NaClAddMount(const char *mount_spec);


/*
 * Returns 1 if virtual filesystem mounts have been set up (enabling filesystem
 * access), 0 otherwise.
 */
int NaClMountsEnabled();


/*
 * Translates a path between host and virtual filesystems. The direction is
 * determined by the to_host flag.
 *
 * @param[in] src_path The source path (virtual if to_host is set, else host).
 * @param[out] dest_path The output path (host if to_host is set, else virtual).
 * @param[in] dest_max_size The size of dest_path buffer.
 * @param[in] to_host Non-zero to translate virtual to host, 0 for backwards.
 * @return 0 on success, else a negated NaCl errno.
 */
uint32_t NaClMountPathTranslate(const char *src_path, char *dest_path,
                                size_t dest_max_size, int to_host);


/*
 * Fills in the current working directory as a sandbox path.
 * Changes working directory to the given sandbox path.
 */
int32_t NaClSandboxGetcwd(char *buf, size_t buf_size);


/*
 * Changes working directory to the given sandbox path.
 * @return 0 on success, else a negated NaCl errno.
 */
int32_t NaClSandboxChdir(const char *path);


/*
 * Given a file path at |src| from the user, copy the path into a buffer |dest|.
 *
 * This function abstracts the complexity of using a "mounted filesystem" --
 * regardless whether sel_ldr is configured to use raw file system access or
 * file system access restricted to a root directory, this function correctly
 * handles the translation from the "raw" user path to the "real" absolute path
 * (which will be prefixed by a root directory, if necessary).
 *
 * @param[in] nap The NaCl application object.
 * @param[out] dest A buffer to contain the user's path argument.
 * @param[in] dest_max_size The size of the dest buffer.
 * @param[in] src A pointer to user's path buffer.
 * @param[in] req_writable If non-zero, require dest to be on a writable mount.
 * @return 0 on success, else a negated NaCl errno.
 */
uint32_t CopyHostPathInFromUser(struct NaClApp *nap, char *dest,
                                size_t dest_max_size, uint32_t src,
                                uint32_t req_writable);


EXTERN_C_END

#endif /* NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_SEL_LDR_FILENAME_H_ */
