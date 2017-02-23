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
int NaClMountsEnabled(void);


/*
 * Converts a sandbox path into a host path. Depending on -a/-m options, it may
 * resolving all symbolic links, extra "/" characters, and references to /./
 * and /../. It produces a host path for use with underlying host filesystem
 * functions. It verifies that the options allow access to the path.
 *
 * @param[in] src The path to be resolved (as seen in the sandbox). It is OK
 *      for src and dest to point to the same buffer.
 * @param[out] dest Buffer to contain the resulting host path. Will be set to
 *      the empty string on error.
 * @param[in] dest_max_size The size of the dest buffer.
 * @param[in] req_writable If non-zero, path must be on a writable mount.
 * @param[in] link_flag Control what to do if the full path is a symlink:
 *      0 to resolve fully, >0 to resolve the path leading up to it, <0 to
 *      disallow it, and return link_flag as the error code in that case.
 * @return 0 on success, else a negated NaCl errno.
 */
int32_t NaClPathToHost(const char *src, char *dest, size_t dest_max_size,
                       int32_t req_writable, int32_t link_flag);

/*
 * Converts a host path into a sandbox path. It is the inverse of
 * NaClPathFromHost. It verifies that the host path is mapped to the sandbox,
 * returning -NACL_ABI_EACCES otherwise.
 *
 * @param[in] src The host path to be converted. May overlap with dest.
 * @param[out] dest Buffer to contain the resulting sandbox path.
 * @param[in] dest_max_size The size of the dest buffer.
 */
int32_t NaClPathFromHost(const char *src, char *dest, size_t dest_max_size);

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
 * @param[in] link_flag When path translation is done, a value of 0 resolves
 *      the final symlink. A positive value resolves the path until the final
 *      symlink. A negative value is an error to return if the path is a symlink.
 * @return 0 on success, else a negated NaCl errno.
 */
uint32_t CopyHostPathInFromUser(struct NaClApp *nap, char *dest,
                                size_t dest_max_size, uint32_t src,
                                int32_t req_writable, int32_t link_flag);


EXTERN_C_END

#endif /* NATIVE_CLIENT_SRC_TRUSTED_SERVICE_RUNTIME_SEL_LDR_FILENAME_H_ */
