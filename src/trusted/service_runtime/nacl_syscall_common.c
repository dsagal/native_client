/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * NaCl service run-time, non-platform specific system call helper routines.
 */
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#if NACL_WINDOWS
#include <windows.h>
#endif

#include "native_client/src/trusted/service_runtime/nacl_syscall_common.h"

#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/include/portability_process.h"
#include "native_client/src/include/portability_string.h"

#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/src/shared/platform/nacl_clock.h"
#include "native_client/src/shared/platform/nacl_exit.h"
#include "native_client/src/shared/platform/nacl_sync_checked.h"
#include "native_client/src/shared/platform/nacl_time.h"

#include "native_client/src/trusted/desc/nacl_desc_base.h"
#include "native_client/src/trusted/desc/nacl_desc_cond.h"
#include "native_client/src/trusted/desc/nacl_desc_mutex.h"
#include "native_client/src/trusted/desc/nacl_desc_semaphore.h"

#include "native_client/src/trusted/service_runtime/include/bits/nacl_syscalls.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"
#include "native_client/src/trusted/service_runtime/include/sys/stat.h"
#include "native_client/src/trusted/service_runtime/include/sys/unistd.h"

#include "native_client/src/trusted/service_runtime/include/sys/nacl_test_crash.h"

#include "native_client/src/trusted/service_runtime/nacl_app_thread.h"
#include "native_client/src/trusted/service_runtime/nacl_copy.h"
#include "native_client/src/trusted/service_runtime/nacl_globals.h"
#include "native_client/src/trusted/service_runtime/nacl_syscall_handlers.h"
#include "native_client/src/trusted/service_runtime/nacl_thread_nice.h"
#include "native_client/src/trusted/service_runtime/nacl_tls.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"


struct NaClDescQuotaInterface;

static size_t const kdefault_io_buffer_bytes_to_log = 64;

/*
 * OSX defines SIZE_T_MAX in i386/limits.h; Linux has SIZE_MAX;
 * Windows has none.
 *
 * TODO(bsy): remove when we put SIZE_T_MAX in a common header file.
 */
#if !defined(SIZE_T_MAX)
# define SIZE_T_MAX   (~(size_t) 0)
#endif

struct NaClSyscallTableEntry nacl_syscall[NACL_MAX_SYSCALLS] = {{0}};


int32_t NaClSysNotImplementedDecoder(struct NaClAppThread *natp) {
  NaClCopyDropLock(natp->nap);
  return -NACL_ABI_ENOSYS;
}

void NaClAddSyscall(int num, int32_t (*fn)(struct NaClAppThread *)) {
  if (nacl_syscall[num].handler != &NaClSysNotImplementedDecoder) {
    NaClLog(LOG_FATAL, "Duplicate syscall number %d\n", num);
  }
  nacl_syscall[num].handler = fn;
}

int32_t NaClSysNull(struct NaClAppThread *natp) {
  UNREFERENCED_PARAMETER(natp);
  return 0;
}

int NaClAclBypassChecks = 0;

void NaClInsecurelyBypassAllAclChecks(void) {
  NaClLog(LOG_WARNING, "BYPASSING ALL ACL CHECKS\n");
  NaClAclBypassChecks = 1;
}

int NaClHighResolutionTimerEnabled(void) {
  return NaClAclBypassChecks;
}

int32_t NaClIoctlAclCheck(struct NaClApp  *nap,
                          struct NaClDesc *ndp,
                          int             request,
                          void            *arg) {
  NaClLog(2,
          ("NaClIoctlAclCheck(0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIxPTR","
           " %d, 0x%08"NACL_PRIxPTR"\n"),
          (uintptr_t) nap, (uintptr_t) ndp, request, (uintptr_t) arg);
  if (NaClAclBypassChecks) {
    return 0;
  }
  return -NACL_ABI_EINVAL;
}

int32_t NaClSysGetpid(struct NaClAppThread *natp) {
  int32_t pid;
  UNREFERENCED_PARAMETER(natp);

  if (NaClAclBypassChecks) {
    pid = GETPID();
  } else {
    pid = -NACL_ABI_EACCES;
  }
  NaClLog(4, "NaClSysGetpid: returning %d\n", pid);

  return pid;
}

int32_t NaClSysExit(struct NaClAppThread  *natp,
                    int                   status) {
  struct NaClApp *nap = natp->nap;

  NaClLog(1, "Exit syscall handler: %d\n", status);

  (void) NaClReportExitStatus(nap, NACL_ABI_W_EXITCODE(status, 0));

  NaClAppThreadTeardown(natp);
  /* NOTREACHED */
  return -NACL_ABI_EINVAL;
}

int32_t NaClSysThreadExit(struct NaClAppThread  *natp,
                          int32_t               *stack_flag) {
  uint32_t  zero = 0;

  NaClLog(4, "NaClSysThreadExit(0x%08"NACL_PRIxPTR", "
          "0x%08"NACL_PRIxPTR"\n",
          (uintptr_t) natp,
          (uintptr_t) stack_flag);
  /*
   * NB: NaClThreads are never joinable, but the abstraction for NaClApps
   * are.
   */

  if (NULL != stack_flag) {
    NaClLog(4,
            "NaClSysThreadExit: stack_flag is %"NACL_PRIxPTR"\n",
            (uintptr_t) stack_flag);
    if (!NaClCopyOutToUser(natp->nap, (uintptr_t) stack_flag,
                           &zero, sizeof zero)) {
      NaClLog(4,
              ("NaClSysThreadExit: ignoring invalid"
               " stack_flag 0x%"NACL_PRIxPTR"\n"),
              (uintptr_t) stack_flag);
    }
  }

  NaClAppThreadTeardown(natp);
  /* NOTREACHED */
  return -NACL_ABI_EINVAL;
}

int32_t NaClSysNameService(struct NaClAppThread *natp,
                           int32_t              *desc_addr) {
  struct NaClApp *nap = natp->nap;
  int32_t   retval = -NACL_ABI_EINVAL;
  int32_t   desc;

  NaClLog(3,
          ("NaClSysNameService(0x%08"NACL_PRIxPTR","
           " 0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp,
          (uintptr_t) desc_addr);

  if (!NaClCopyInFromUser(nap, &desc, (uintptr_t) desc_addr, sizeof desc)) {
    NaClLog(LOG_ERROR,
            "Invalid address argument to NaClSysNameService\n");
    retval = -NACL_ABI_EFAULT;
    goto done;
  }

  if (-1 == desc) {
    /* read */
    desc = NaClSetAvail(nap, NaClDescRef(nap->name_service_conn_cap));
    if (NaClCopyOutToUser(nap, (uintptr_t) desc_addr,
                          &desc, sizeof desc)) {
      retval = 0;
    } else {
      retval = -NACL_ABI_EFAULT;
    }
  } else {
    struct NaClDesc *desc_obj_ptr = NaClGetDesc(nap, desc);

    if (NULL == desc_obj_ptr) {
      retval = -NACL_ABI_EBADF;
      goto done;
    }
    if (NACL_DESC_CONN_CAP != NACL_VTBL(NaClDesc, desc_obj_ptr)->typeTag &&
        NACL_DESC_CONN_CAP_FD != NACL_VTBL(NaClDesc, desc_obj_ptr)->typeTag) {
      retval = -NACL_ABI_EINVAL;
      goto done;
    }
    /* write */
    NaClXMutexLock(&nap->mu);
    NaClDescUnref(nap->name_service_conn_cap);
    nap->name_service_conn_cap = desc_obj_ptr;
    NaClXMutexUnlock(&nap->mu);
    retval = 0;
  }

 done:
  return retval;
}

int32_t NaClSysDup(struct NaClAppThread *natp,
                   int                  oldfd) {
  struct NaClApp  *nap = natp->nap;
  int             retval;
  struct NaClDesc *old_nd;

  NaClLog(3, "NaClSysDup(0x%08"NACL_PRIxPTR", %d)\n",
          (uintptr_t) natp, oldfd);
  old_nd = NaClGetDesc(nap, oldfd);
  if (NULL == old_nd) {
    retval = -NACL_ABI_EBADF;
    goto done;
  }
  retval = NaClSetAvail(nap, old_nd);
done:
  return retval;
}

int32_t NaClSysDup2(struct NaClAppThread  *natp,
                    int                   oldfd,
                    int                   newfd) {
  struct NaClApp  *nap = natp->nap;
  int             retval;
  struct NaClDesc *old_nd;

  NaClLog(3, "NaClSysDup(0x%08"NACL_PRIxPTR", %d, %d)\n",
          (uintptr_t) natp, oldfd, newfd);
  if (newfd < 0) {
    retval = -NACL_ABI_EINVAL;
    goto done;
  }
  /*
   * TODO(bsy): is this a reasonable largest sane value?  The
   * descriptor array shouldn't get too large.
   */
  if (newfd >= NACL_MAX_FD) {
    retval = -NACL_ABI_EINVAL;
    goto done;
  }
  old_nd = NaClGetDesc(nap, oldfd);
  if (NULL == old_nd) {
    retval = -NACL_ABI_EBADF;
    goto done;
  }
  NaClSetDesc(nap, newfd, old_nd);
  retval = newfd;
done:
  return retval;
}

int32_t NaClSysClose(struct NaClAppThread *natp,
                     int                  d) {
  struct NaClApp  *nap = natp->nap;
  int             retval = -NACL_ABI_EBADF;
  struct NaClDesc *ndp;

  NaClLog(3, "Entered NaClSysClose(0x%08"NACL_PRIxPTR", %d)\n",
          (uintptr_t) natp, d);

  NaClFastMutexLock(&nap->desc_mu);
  ndp = NaClGetDescMu(nap, d);
  if (NULL != ndp) {
    NaClSetDescMu(nap, d, NULL);  /* Unref the desc_tbl */
  }
  NaClFastMutexUnlock(&nap->desc_mu);
  NaClLog(5, "Invoking Close virtual function of object 0x%08"NACL_PRIxPTR"\n",
          (uintptr_t) ndp);
  if (NULL != ndp) {
    NaClDescUnref(ndp);
    retval = 0;
  }

  return retval;
}

int32_t NaClSysGetdents(struct NaClAppThread *natp,
                        int                  d,
                        void                 *dirp,
                        size_t               count) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  ssize_t         getdents_ret;
  uintptr_t       sysaddr;
  struct NaClDesc *ndp;

  NaClLog(3,
          ("Entered NaClSysGetdents(0x%08"NACL_PRIxPTR", "
           "%d, 0x%08"NACL_PRIxPTR", "
           "%"NACL_PRIdS"[0x%"NACL_PRIxS"])\n"),
          (uintptr_t) natp, d, (uintptr_t) dirp, count, count);

  ndp = NaClGetDesc(nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  /*
   * Generic NaClCopyOutToUser is not sufficient, since buffer size
   * |count| is arbitrary and we wouldn't want to have to allocate
   * memory in trusted address space to match.
   */
  sysaddr = NaClUserToSysAddrRange(nap, (uintptr_t) dirp, count);
  if (kNaClBadAddress == sysaddr) {
    NaClLog(4, " illegal address for directory data\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup_unref;
  }

  /*
   * Clamp count to INT32_MAX to avoid the possibility of Getdents returning
   * a value that is outside the range of an int32.
   */
  if (count > INT32_MAX) {
    count = INT32_MAX;
  }
  /*
   * Grab addr space lock; getdents should not normally block, though
   * if the directory is on a networked filesystem this could, and
   * cause mmap to be slower on Windows.
   */
  NaClXMutexLock(&nap->mu);
  getdents_ret = (*((struct NaClDescVtbl const *) ndp->base.vtbl)->
                  Getdents)(ndp,
                            (void *) sysaddr,
                            count);
  NaClXMutexUnlock(&nap->mu);
  /* drop addr space lock */
  if ((getdents_ret < INT32_MIN && !NaClSSizeIsNegErrno(&getdents_ret))
      || INT32_MAX < getdents_ret) {
    /* This should never happen, because we already clamped the input count */
    NaClLog(LOG_FATAL, "Overflow in Getdents: return value is %"NACL_PRIxS,
            getdents_ret);
  } else {
    retval = (int32_t) getdents_ret;
  }
  if (retval > 0) {
    NaClLog(4, "getdents returned %d bytes\n", retval);
    NaClLog(8, "getdents result: %.*s\n", retval, (char *) sysaddr);
  } else {
    NaClLog(4, "getdents returned %d\n", retval);
  }

cleanup_unref:
  NaClDescUnref(ndp);

cleanup:
  return retval;
}

int32_t NaClSysRead(struct NaClAppThread  *natp,
                    int                   d,
                    void                  *buf,
                    size_t                count) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  ssize_t         read_result = -NACL_ABI_EINVAL;
  uintptr_t       sysaddr;
  struct NaClDesc *ndp;
  size_t          log_bytes;
  char const      *ellipsis = "";

  NaClLog(3,
          ("Entered NaClSysRead(0x%08"NACL_PRIxPTR", "
           "%d, 0x%08"NACL_PRIxPTR", "
           "%"NACL_PRIdS"[0x%"NACL_PRIxS"])\n"),
          (uintptr_t) natp, d, (uintptr_t) buf, count, count);

  ndp = NaClGetDesc(nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  sysaddr = NaClUserToSysAddrRange(nap, (uintptr_t) buf, count);
  if (kNaClBadAddress == sysaddr) {
    NaClDescUnref(ndp);
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  /*
   * The maximum length for read and write is INT32_MAX--anything larger and
   * the return value would overflow. Passing larger values isn't an error--
   * we'll just clamp the request size if it's too large.
   */
  if (count > INT32_MAX) {
    count = INT32_MAX;
  }

  NaClVmIoWillStart(nap,
                    (uint32_t) (uintptr_t) buf,
                    (uint32_t) (((uintptr_t) buf) + count - 1));
  read_result = (*((struct NaClDescVtbl const *) ndp->base.vtbl)->
                 Read)(ndp, (void *) sysaddr, count);
  NaClVmIoHasEnded(nap,
                    (uint32_t) (uintptr_t) buf,
                    (uint32_t) (((uintptr_t) buf) + count - 1));
  if (read_result > 0) {
    NaClLog(4, "read returned %"NACL_PRIdS" bytes\n", read_result);
    log_bytes = (size_t) read_result;
    if (log_bytes > INT32_MAX) {
      log_bytes = INT32_MAX;
      ellipsis = "...";
    }
    if (NaClLogGetVerbosity() < 10) {
      if (log_bytes > kdefault_io_buffer_bytes_to_log) {
        log_bytes = kdefault_io_buffer_bytes_to_log;
        ellipsis = "...";
      }
    }
    NaClLog(8, "read result: %.*s%s\n",
            (int) log_bytes, (char *) sysaddr, ellipsis);
  } else {
    NaClLog(4, "read returned %"NACL_PRIdS"\n", read_result);
  }
  NaClDescUnref(ndp);

  /* This cast is safe because we clamped count above.*/
  retval = (int32_t) read_result;
cleanup:
  return retval;
}

int32_t NaClSysWrite(struct NaClAppThread *natp,
                     int                  d,
                     void                 *buf,
                     size_t               count) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  ssize_t         write_result = -NACL_ABI_EINVAL;
  uintptr_t       sysaddr;
  char const      *ellipsis = "";
  struct NaClDesc *ndp;
  size_t          log_bytes;

  NaClLog(3,
          "Entered NaClSysWrite(0x%08"NACL_PRIxPTR", "
          "%d, 0x%08"NACL_PRIxPTR", "
          "%"NACL_PRIdS"[0x%"NACL_PRIxS"])\n",
          (uintptr_t) natp, d, (uintptr_t) buf, count, count);

  ndp = NaClGetDesc(nap, d);
  NaClLog(4, " ndp = %"NACL_PRIxPTR"\n", (uintptr_t) ndp);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  sysaddr = NaClUserToSysAddrRange(nap, (uintptr_t) buf, count);
  if (kNaClBadAddress == sysaddr) {
    NaClDescUnref(ndp);
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  log_bytes = count;
  if (log_bytes > INT32_MAX) {
    log_bytes = INT32_MAX;
    ellipsis = "...";
  }
  if (NaClLogGetVerbosity() < 10) {
    if (log_bytes > kdefault_io_buffer_bytes_to_log) {
      log_bytes = kdefault_io_buffer_bytes_to_log;
      ellipsis = "...";
    }
  }
  NaClLog(8, "In NaClSysWrite(%d, %.*s%s, %"NACL_PRIdS")\n",
          d, (int) log_bytes, (char *) sysaddr, ellipsis, count);

  /*
   * The maximum length for read and write is INT32_MAX--anything larger and
   * the return value would overflow. Passing larger values isn't an error--
   * we'll just clamp the request size if it's too large.
   */
  if (count > INT32_MAX) {
    count = INT32_MAX;
  }

  NaClVmIoWillStart(nap,
                    (uint32_t) (uintptr_t) buf,
                    (uint32_t) (((uintptr_t) buf) + count - 1));
  write_result = (*((struct NaClDescVtbl const *) ndp->base.vtbl)->
                  Write)(ndp, (void *) sysaddr, count);
  NaClVmIoHasEnded(nap,
                   (uint32_t) (uintptr_t) buf,
                   (uint32_t) (((uintptr_t) buf) + count - 1));

  NaClDescUnref(ndp);

  /* This cast is safe because we clamped count above.*/
  retval = (int32_t) write_result;

cleanup:
  return retval;
}

/*
 * This implements 64-bit offsets, so we use |offp| as an in/out
 * address so we can have a 64 bit return value.
 */
int32_t NaClSysLseek(struct NaClAppThread *natp,
                     int                  d,
                     nacl_abi_off_t       *offp,
                     int                  whence) {
  struct NaClApp  *nap = natp->nap;
  nacl_abi_off_t  offset;
  nacl_off64_t    retval64;
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *ndp;

  NaClLog(3,
          ("Entered NaClSysLseek(0x%08"NACL_PRIxPTR", %d,"
           " 0x%08"NACL_PRIxPTR", %d)\n"),
          (uintptr_t) natp, d, (uintptr_t) offp, whence);

  ndp = NaClGetDesc(nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  if (!NaClCopyInFromUser(nap, &offset, (uintptr_t) offp, sizeof offset)) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup_unref;
  }
  NaClLog(4, "offset 0x%08"NACL_PRIxNACL_OFF"\n", offset);

  retval64 = (*((struct NaClDescVtbl const *) ndp->base.vtbl)->
              Seek)(ndp, (nacl_off64_t) offset, whence);
  if (NaClOff64IsNegErrno(&retval64)) {
    retval = (int32_t) retval64;
  } else {
    if (NaClCopyOutToUser(nap, (uintptr_t) offp, &retval64, sizeof retval64)) {
      retval = 0;
    } else {
      NaClLog(LOG_FATAL,
              "NaClSysLseek: in/out ptr became invalid at copyout?\n");
    }
  }
cleanup_unref:
  NaClDescUnref(ndp);
cleanup:
  return retval;
}

int32_t NaClSysIoctl(struct NaClAppThread *natp,
                     int                  d,
                     int                  request,
                     void                 *arg) {
  struct NaClApp  *nap = natp->nap;
  int             retval = -NACL_ABI_EINVAL;
  uintptr_t       sysaddr;
  struct NaClDesc *ndp;

  NaClLog(3,
          ("Entered NaClSysIoctl(0x%08"NACL_PRIxPTR
           ", %d, %d, 0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp, d, request,
          (uintptr_t) arg);
  /*
   * Note that NaClUserToSysAddrRange is not feasible right now, since
   * the size of the arg argument depends on the request.  We do not
   * have an enumeration of allowed ioctl requests yet.
   *
   * Furthermore, some requests take no arguments, so sysaddr might
   * end up being kNaClBadAddress and that is perfectly okay.
   */
  sysaddr = NaClUserToSysAddr(nap, (uintptr_t) arg);
  /*
   ****************************************
   * NOTE: sysaddr may be kNaClBadAddress *
   ****************************************
   */

  ndp = NaClGetDesc(nap, d);
  if (NULL == ndp) {
    NaClLog(4, "bad desc\n");
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = NaClIoctlAclCheck(nap, ndp, request, arg);
  if (0 != retval) {
    NaClLog(3, "Ioctl ACL check rejected descriptor %d\n", d);
    goto cleanup_unref;
  }

  /*
   * We need a virtual function that, given request, returns max
   * anticipated buffer size so we can do the right thing wrt VM locks
   * if the ioctl might be blocking.  For now, we assume that ioctls
   * aren't.  Since we have at least 1 guard page, even if |arg|
   * points to near the end of the address space, we should be fine
   * for reasonable sizes of arguments from the point of view of
   * staying within the untrusted address space.
   */
  NaClXMutexLock(&nap->mu);
  retval = (*((struct NaClDescVtbl const *) ndp->base.vtbl)->
            Ioctl)(ndp, request, (void *) sysaddr);
  NaClXMutexUnlock(&nap->mu);
cleanup_unref:
  NaClDescUnref(ndp);
cleanup:
  return retval;
}


int32_t NaClSysFstat(struct NaClAppThread *natp,
                     int                  d,
                     struct nacl_abi_stat *nasp) {
  struct NaClApp        *nap = natp->nap;
  int32_t               retval = -NACL_ABI_EINVAL;
  struct NaClDesc       *ndp;
  struct nacl_abi_stat  result;

  NaClLog(3,
          ("Entered NaClSysFstat(0x%08"NACL_PRIxPTR
           ", %d, 0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp,
          d, (uintptr_t) nasp);

  NaClLog(4,
          " sizeof(struct nacl_abi_stat) = %"NACL_PRIdS" (0x%"NACL_PRIxS")\n",
          sizeof *nasp, sizeof *nasp);

  ndp = NaClGetDesc(nap, d);
  if (NULL == ndp) {
    NaClLog(4, "bad desc\n");
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) ndp->base.vtbl)->
            Fstat)(ndp, &result);
  if (0 == retval) {
    if (!NaClCopyOutToUser(nap, (uintptr_t) nasp,
                           &result, sizeof result)) {
      retval = -NACL_ABI_EFAULT;
    }
  }

  NaClDescUnref(ndp);
cleanup:
  return retval;
}

int32_t NaClSysTlsInit(struct NaClAppThread  *natp,
                       uint32_t              thread_ptr) {
  int32_t   retval = -NACL_ABI_EINVAL;
  uintptr_t sys_tls;

  NaClLog(3,
          ("Entered NaClSysTlsInit(0x%08"NACL_PRIxPTR
           ", 0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp, (uintptr_t) thread_ptr);

  /* Verify that the address in the app's range and translated from
   * nacl module address to service runtime address - a nop on ARM
   */
  sys_tls = NaClUserToSysAddrRange(natp->nap, thread_ptr, 4);
  NaClLog(4,
          "NaClSysTlsInit: thread_ptr 0x%"NACL_PRIx32
          ", sys_tls 0x%"NACL_PRIxPTR"\n",
          thread_ptr, sys_tls);
  if (kNaClBadAddress == sys_tls) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  NaClTlsSetTlsValue1(natp, thread_ptr);
  retval = 0;
cleanup:
  return retval;
}

int32_t NaClSysThreadCreate(struct NaClAppThread *natp,
                            void                 *prog_ctr,
                            uint32_t             stack_ptr,
                            uint32_t             thread_ptr,
                            uint32_t             second_thread_ptr) {
  struct NaClApp *nap = natp->nap;
  int32_t     retval = -NACL_ABI_EINVAL;
  uintptr_t   sys_tls;
  uintptr_t   sys_stack;

  NaClLog(3,
          ("Entered NaClSysThreadCreate(0x%08"NACL_PRIxPTR
           " pc=0x%08"NACL_PRIxPTR", sp=0x%08"NACL_PRIx32", thread_ptr=0x%08"
           NACL_PRIx32")\n"),
          (uintptr_t) natp, (uintptr_t) prog_ctr, stack_ptr, thread_ptr);

  if (!NaClIsValidJumpTarget(nap, (uintptr_t) prog_ctr)) {
    NaClLog(LOG_ERROR, "NaClSysThreadCreate: Bad function pointer\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  /* Align the stack pointer. */
  stack_ptr = ((stack_ptr + NACL_STACK_PAD_BELOW_ALIGN)
               & ~NACL_STACK_ALIGN_MASK) - NACL_STACK_PAD_BELOW_ALIGN
              - NACL_STACK_ARGS_SIZE;

  sys_stack = NaClUserToSysAddr(nap, stack_ptr);
  if (kNaClBadAddress == sys_stack) {
    NaClLog(LOG_ERROR, "bad stack\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  sys_tls = NaClUserToSysAddrRange(nap, thread_ptr, 4);
  if (kNaClBadAddress == sys_tls) {
    NaClLog(LOG_ERROR, "bad TLS pointer\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  NaClVmHoleWaitToStartThread(nap);

  retval = NaClCreateAdditionalThread(nap,
                                      (uintptr_t) prog_ctr,
                                      sys_stack,
                                      thread_ptr,
                                      second_thread_ptr);

cleanup:
  return retval;
}

/*
 * This is not used on x86-64 and its functionality is replaced by
 * NaClGetTlsFastPath1 (see nacl_syscall_64.S).
 */
int32_t NaClSysTlsGet(struct NaClAppThread *natp) {
  return NaClTlsGetTlsValue1(natp);
}

int32_t NaClSysSecondTlsSet(struct NaClAppThread *natp,
                            uint32_t             new_value) {
  NaClTlsSetTlsValue2(natp, new_value);
  return 0;
}

/*
 * This is not used on x86-64 and its functionality is replaced by
 * NaClGetTlsFastPath2 (see nacl_syscall_64.S).
 */
int32_t NaClSysSecondTlsGet(struct NaClAppThread *natp) {
  return NaClTlsGetTlsValue2(natp);
}

int NaClSysThreadNice(struct NaClAppThread *natp,
                      const int            nice) {
  /* Note: implementation of nacl_thread_nice is OS dependent. */
  UNREFERENCED_PARAMETER(natp);
  return nacl_thread_nice(nice);
}

int32_t NaClSysMutexCreate(struct NaClAppThread *natp) {
  struct NaClApp       *nap = natp->nap;
  int32_t              retval = -NACL_ABI_EINVAL;
  struct NaClDescMutex *desc;

  NaClLog(3,
          ("Entered NaClSysMutexCreate(0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp);

  desc = malloc(sizeof(*desc));

  if (!desc || !NaClDescMutexCtor(desc)) {
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }

  retval = NaClSetAvail(nap, (struct NaClDesc *) desc);
  desc = NULL;
cleanup:
  free(desc);
  NaClLog(3,
          ("NaClSysMutexCreate(0x%08"NACL_PRIxPTR") = %d\n"),
          (uintptr_t) natp, retval);
  return retval;
}

int32_t NaClSysMutexLock(struct NaClAppThread  *natp,
                         int32_t               mutex_handle) {
  struct NaClApp        *nap = natp->nap;
  int32_t               retval = -NACL_ABI_EINVAL;
  struct NaClDesc       *desc;

  NaClLog(3,
          ("Entered NaClSysMutexLock(0x%08"NACL_PRIxPTR", %d)\n"),
          (uintptr_t) natp, mutex_handle);

  desc = NaClGetDesc(nap, mutex_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) desc->base.vtbl)->Lock)(desc);
  NaClDescUnref(desc);

cleanup:
  return retval;
}

int32_t NaClSysMutexUnlock(struct NaClAppThread  *natp,
                           int32_t               mutex_handle) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClLog(3,
          ("Entered NaClSysMutexUnlock(0x%08"NACL_PRIxPTR", %d)\n"),
          (uintptr_t) natp, mutex_handle);

  desc = NaClGetDesc(nap, mutex_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) desc->base.vtbl)->Unlock)(desc);
  NaClDescUnref(desc);

cleanup:
  return retval;
}

int32_t NaClSysMutexTrylock(struct NaClAppThread   *natp,
                            int32_t                 mutex_handle) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClLog(3,
          ("Entered NaClSysMutexTrylock(0x%08"NACL_PRIxPTR", %d)\n"),
          (uintptr_t) natp, mutex_handle);

  desc = NaClGetDesc(nap, mutex_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) desc->base.vtbl)->TryLock)(desc);
  NaClDescUnref(desc);

cleanup:
  return retval;
}

int32_t NaClSysCondCreate(struct NaClAppThread *natp) {
  struct NaClApp         *nap = natp->nap;
  int32_t                retval = -NACL_ABI_EINVAL;
  struct NaClDescCondVar *desc;

  NaClLog(3,
          ("Entered NaClSysCondCreate(0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp);

  desc = malloc(sizeof(*desc));

  if (!desc || !NaClDescCondVarCtor(desc)) {
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }

  retval = NaClSetAvail(nap, (struct NaClDesc *)desc);
  desc = NULL;
cleanup:
  free(desc);
  NaClLog(3,
          ("NaClSysCondCreate(0x%08"NACL_PRIxPTR") = %d\n"),
          (uintptr_t) natp, retval);
  return retval;
}

int32_t NaClSysCondWait(struct NaClAppThread *natp,
                        int32_t              cond_handle,
                        int32_t              mutex_handle) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *cv_desc;
  struct NaClDesc *mutex_desc;

  NaClLog(3,
          ("Entered NaClSysCondWait(0x%08"NACL_PRIxPTR", %d, %d)\n"),
          (uintptr_t) natp, cond_handle, mutex_handle);

  cv_desc = NaClGetDesc(nap, cond_handle);

  if (NULL == cv_desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  mutex_desc = NaClGetDesc(nap, mutex_handle);
  if (NULL == mutex_desc) {
    NaClDescUnref(cv_desc);
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) cv_desc->base.vtbl)->
            Wait)(cv_desc, mutex_desc);
  NaClDescUnref(cv_desc);
  NaClDescUnref(mutex_desc);

cleanup:
  return retval;
}

int32_t NaClSysCondSignal(struct NaClAppThread *natp,
                          int32_t              cond_handle) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClLog(3,
          ("Entered NaClSysCondSignal(0x%08"NACL_PRIxPTR", %d)\n"),
          (uintptr_t) natp, cond_handle);

  desc = NaClGetDesc(nap, cond_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) desc->base.vtbl)->Signal)(desc);
  NaClDescUnref(desc);
cleanup:
  return retval;
}

int32_t NaClSysCondBroadcast(struct NaClAppThread  *natp,
                             int32_t               cond_handle) {
  struct NaClApp  *nap = natp->nap;
  struct NaClDesc *desc;
  int32_t         retval = -NACL_ABI_EINVAL;

  NaClLog(3,
          ("Entered NaClSysCondBroadcast(0x%08"NACL_PRIxPTR", %d)\n"),
          (uintptr_t) natp, cond_handle);

  desc = NaClGetDesc(nap, cond_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) desc->base.vtbl)->Broadcast)(desc);
  NaClDescUnref(desc);

cleanup:
  return retval;
}

int32_t NaClSysCondTimedWaitAbs(struct NaClAppThread     *natp,
                                int32_t                  cond_handle,
                                int32_t                  mutex_handle,
                                struct nacl_abi_timespec *ts) {
  struct NaClApp           *nap = natp->nap;
  int32_t                  retval = -NACL_ABI_EINVAL;
  struct NaClDesc          *cv_desc;
  struct NaClDesc          *mutex_desc;
  struct nacl_abi_timespec trusted_ts;

  NaClLog(3,
          ("Entered NaClSysCondTimedWaitAbs(0x%08"NACL_PRIxPTR
           ", %d, %d, 0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp, cond_handle, mutex_handle, (uintptr_t) ts);

  if (!NaClCopyInFromUser(nap, &trusted_ts,
                          (uintptr_t) ts, sizeof trusted_ts)) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  /* TODO(gregoryd): validate ts - do we have a limit for time to wait? */

  cv_desc = NaClGetDesc(nap, cond_handle);
  if (NULL == cv_desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  mutex_desc = NaClGetDesc(nap, mutex_handle);
  if (NULL == mutex_desc) {
    NaClDescUnref(cv_desc);
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) cv_desc->base.vtbl)->
            TimedWaitAbs)(cv_desc,
                          mutex_desc,
                          &trusted_ts);
  NaClDescUnref(cv_desc);
  NaClDescUnref(mutex_desc);
cleanup:
  return retval;
}

int32_t NaClSysSemCreate(struct NaClAppThread *natp,
                         int32_t              init_value) {
  struct NaClApp           *nap = natp->nap;
  int32_t                  retval = -NACL_ABI_EINVAL;
  struct NaClDescSemaphore *desc;

  NaClLog(3,
          ("Entered NaClSysSemCreate(0x%08"NACL_PRIxPTR
           ", %d)\n"),
          (uintptr_t) natp, init_value);

  desc = malloc(sizeof(*desc));

  if (!desc || !NaClDescSemaphoreCtor(desc, init_value)) {
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }

  retval = NaClSetAvail(nap, (struct NaClDesc *) desc);
  desc = NULL;
cleanup:
  free(desc);
  return retval;
}


int32_t NaClSysSemWait(struct NaClAppThread *natp,
                       int32_t              sem_handle) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClLog(3,
          ("Entered NaClSysSemWait(0x%08"NACL_PRIxPTR
           ", %d)\n"),
          (uintptr_t) natp, sem_handle);

  desc = NaClGetDesc(nap, sem_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  /*
   * TODO(gregoryd): we have to decide on the syscall API: do we
   * switch to read/write/ioctl API or do we stay with the more
   * detailed API. Anyway, using a single syscall for waiting on all
   * synchronization objects makes sense.
   */
  retval = (*((struct NaClDescVtbl const *) desc->base.vtbl)->SemWait)(desc);
  NaClDescUnref(desc);
cleanup:
  return retval;
}

int32_t NaClSysSemPost(struct NaClAppThread *natp,
                       int32_t              sem_handle) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClLog(3,
          ("Entered NaClSysSemPost(0x%08"NACL_PRIxPTR
           ", %d)\n"),
          (uintptr_t) natp, sem_handle);

  desc = NaClGetDesc(nap, sem_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) desc->base.vtbl)->Post)(desc);
  NaClDescUnref(desc);
cleanup:
  return retval;
}

int32_t NaClSysSemGetValue(struct NaClAppThread *natp,
                           int32_t              sem_handle) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClLog(3,
          ("Entered NaClSysSemGetValue(0x%08"NACL_PRIxPTR
           ", %d)\n"),
          (uintptr_t) natp, sem_handle);

  desc = NaClGetDesc(nap, sem_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*((struct NaClDescVtbl const *) desc->base.vtbl)->GetValue)(desc);
  NaClDescUnref(desc);
cleanup:
  return retval;
}

int32_t NaClSysNanosleep(struct NaClAppThread     *natp,
                         struct nacl_abi_timespec *req,
                         struct nacl_abi_timespec *rem) {
  struct NaClApp            *nap = natp->nap;
  struct nacl_abi_timespec  t_sleep;
  struct nacl_abi_timespec  t_rem;
  struct nacl_abi_timespec  *remptr;
  int                       retval = -NACL_ABI_EINVAL;

  NaClLog(3,
          ("Entered NaClSysNanosleep(0x%08"NACL_PRIxPTR
           ", 0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIxPTR"x)\n"),
          (uintptr_t) natp, (uintptr_t) req, (uintptr_t) rem);

  /* do the check before we sleep */
  if (NULL != rem && kNaClBadAddress ==
      NaClUserToSysAddrRange(nap, (uintptr_t) rem, sizeof *rem)) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  if (!NaClCopyInFromUser(nap, &t_sleep,
                          (uintptr_t) req, sizeof t_sleep)) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  remptr = (NULL == rem) ? NULL : &t_rem;
  /* NULL != remptr \equiv NULL != rem */

  /*
   * We assume that we do not need to normalize the time request values.
   *
   * If bogus values can cause the underlying OS to get into trouble,
   * then we need more checking here.
   */
  NaClLog(4, "NaClSysNanosleep(time = %"NACL_PRId64".%09"NACL_PRId64" S)\n",
          (int64_t) t_sleep.tv_sec, (int64_t) t_sleep.tv_nsec);
  retval = NaClNanosleep(&t_sleep, remptr);
  NaClLog(4, "NaClNanosleep returned %d\n", retval);

  if (-EINTR == retval && NULL != rem &&
      !NaClCopyOutToUser(nap, (uintptr_t) rem, remptr, sizeof *remptr)) {
    NaClLog(LOG_FATAL, "NaClSysNanosleep: check rem failed at copyout\n");
  }

cleanup:
  NaClLog(4, "nanosleep done.\n");
  return retval;
}

int32_t NaClSysSchedYield(struct NaClAppThread *natp) {
  UNREFERENCED_PARAMETER(natp);

  NaClThreadYield();
  return 0;
}

int32_t NaClSysSysconf(struct NaClAppThread *natp,
                       int32_t              name,
                       int32_t              *result) {
  struct NaClApp  *nap = natp->nap;
  int32_t         retval = -NACL_ABI_EINVAL;
  int32_t         result_value;

  NaClLog(3,
          ("Entered NaClSysSysconf(%08"NACL_PRIxPTR
           "x, %d, 0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp, name, (uintptr_t) result);

  switch (name) {
    case NACL_ABI__SC_NPROCESSORS_ONLN: {
#if NACL_WINDOWS
      static int32_t number_of_workers = 0;
      if (0 == number_of_workers) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        number_of_workers = (int32_t) si.dwNumberOfProcessors;
      }
      result_value = number_of_workers;
#elif NACL_LINUX || NACL_OSX
      if (-1 == nap->sc_nprocessors_onln) {
        /* Unable to get the number of processors at startup. */
        goto cleanup;
      }
      result_value = nap->sc_nprocessors_onln;
#else
#error Unsupported platform
#endif
      break;
    }
    case NACL_ABI__SC_SENDMSG_MAX_SIZE: {
      /* TODO(sehr,bsy): this value needs to be determined at run time. */
      const int32_t kImcSendMsgMaxSize = 1 << 16;
      result_value = kImcSendMsgMaxSize;
      break;
    }
    case NACL_ABI__SC_PAGESIZE: {
      result_value = 1 << 16;  /* always 64k pages */
      break;
    }
    case NACL_ABI__SC_NACL_FILE_ACCESS_ENABLED: {
      result_value = NaClAclBypassChecks;
      break;
    }
    case NACL_ABI__SC_NACL_LIST_MAPPINGS_ENABLED: {
      result_value = nap->enable_list_mappings;
      break;
    }
    default: {
      retval = -NACL_ABI_EINVAL;
      goto cleanup;
    }
  }
  if (!NaClCopyOutToUser(nap, (uintptr_t) result, &result_value,
                         sizeof result_value)) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  retval = 0;
cleanup:
  return retval;
}

int32_t NaClSysTestInfoLeak(struct NaClAppThread *natp) {
#if NACL_ARCH(NACL_BUILD_ARCH) == NACL_x86
  /*
   * Put some interesting bits into the x87 and SSE registers.
   */
  union fxsave {
    char buf[512];
    struct {
      uint16_t fcw;
      uint16_t fsw;
      uint16_t ftw;
      uint16_t fop;
      union {
        struct {
          uint64_t rip;
          uint64_t rdp;
        } x64;
        struct {
          uint32_t fpu_ip;
          uint32_t cs;
          uint32_t fpu_dp;
          uint32_t ds;
        } ia32;
      } bitness;
      uint32_t mxcsr;
      uint32_t mxcsr_mask;
      struct {
        uint8_t st[10];
        uint8_t reserved[6];
      } st_space[8];
      uint32_t xmm_space[64];
    } fxsave;
  };

  static const char tenbytes[10] = "SecretBits";
  static const char manybytes[256] =
      "Highly sensitive information must not be leaked to untrusted code!\n"
      "xyzzy\nplugh\nYou are likely to be eaten by a grue.\n"
      "When in the Course of human events it becomes necessary for one people"
      " to dissolve the political bands which have connected them with ...\n";

# ifdef __GNUC__
  union fxsave u __attribute__((aligned(16)));
# elif NACL_WINDOWS
  __declspec(align(16)) union fxsave u;
# else
#  error Unsupported platform
# endif

  int i;

# ifdef __GNUC__
  __asm__("fxsave %0" : "=m" (u));
# elif NACL_WINDOWS
#  if NACL_BUILD_SUBARCH == 64
  NaClDoFxsave(&u);
#  else
  __asm {
    fxsave u
  };
#  endif
# else
# error Unsupported platform
# endif

  for (i = 0; i < 8; ++i)
    memcpy(&u.fxsave.st_space[i], tenbytes, sizeof(tenbytes));

  memcpy(u.fxsave.xmm_space, manybytes, sizeof(u.fxsave.xmm_space));

  /*
   * Set the MXCSR to an unlikely (but valid) value: all valid bits set.
   * The mask is provided by the hardware to say which bits can be set
   * (all others are reserved).  The untrusted test code (in
   * tests/infoleak/test_infoleak.c) sets MXCSR to zero before
   * making this system call so this value ensures that the test
   * actually verifies the behavior of the syscall return path.
   */
  u.fxsave.mxcsr = u.fxsave.mxcsr_mask;

# ifdef __GNUC__
  __asm__ volatile("fxrstor %0" :: "m" (u));
# elif NACL_WINDOWS
#  if NACL_BUILD_SUBARCH == 64
  NaClDoFxrstor(&u);
#  else
  __asm {
    fxrstor u
  };
#  endif
# else
# error Unsupported platform
# endif

#elif NACL_ARCH(NACL_BUILD_ARCH) == NACL_arm
  /*
   * Put some interesting bits into the VFP registers.
   */

  static const char manybytes[64] =
      "Sensitive information must not be leaked to untrusted code!!!!\n";

  __asm__ volatile("vldm %0, {d0-d7}" :: "r" (manybytes) :
                   "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7");
  __asm__ volatile("fmxr fpscr, %0" :: "r" (0xdeadbeef) : "vfpcc");

#endif

  UNREFERENCED_PARAMETER(natp);

  return -NACL_ABI_ENOSYS;
}

/*
 * This syscall is intended for testing NaCl's support for Breakpad
 * crash reporting inside Chromium.  When
 * http://code.google.com/p/nativeclient/issues/detail?id=579 is
 * addressed, we might put this syscall behind a flag.  Until then,
 * untrusted code can trigger Breakpad-reported crashes inside
 * syscalls, so there is no benefit to restricting this syscall.
 */
int32_t NaClSysTestCrash(struct NaClAppThread *natp, int crash_type) {
  /*
   * Despite being volatile, the Apple system compiler, llvm-gcc, still
   * optimizes the null pointer dereference into an illegal instruction when
   * written as a one-liner. That interferes with tests that expect precisely
   * a SIGSEGV, because they'll see a SIGILL instead.
   */
  volatile int *volatile p = 0;
  UNREFERENCED_PARAMETER(natp);

  switch (crash_type) {
    case NACL_TEST_CRASH_MEMORY:
      *p = 0;
      break;
    case NACL_TEST_CRASH_LOG_FATAL:
      NaClLog(LOG_FATAL, "NaClSysTestCrash: This is a test error\n");
      break;
    case NACL_TEST_CRASH_CHECK_FAILURE:
      CHECK(0);
      break;
  }
  return -NACL_ABI_EINVAL;
}

int32_t NaClSysGetTimeOfDay(struct NaClAppThread      *natp,
                            struct nacl_abi_timeval   *tv,
                            struct nacl_abi_timezone  *tz) {
  int                     retval;
  struct nacl_abi_timeval now;

  UNREFERENCED_PARAMETER(tz);

  NaClLog(3,
          ("Entered NaClSysGetTimeOfDay(%08"NACL_PRIxPTR
           ", 0x%08"NACL_PRIxPTR", 0x%08"NACL_PRIxPTR")\n"),
          (uintptr_t) natp, (uintptr_t) tv, (uintptr_t) tz);

  /*
   * tz is not supported in linux, nor is it supported by glibc, since
   * tzset(3) and the zoneinfo file should be used instead.
   *
   * TODO(bsy) Do we make the zoneinfo directory available to
   * applications?
   */

  retval = NaClGetTimeOfDay(&now);
  if (0 != retval) {
    return retval;
  }
#if !NACL_WINDOWS
  /*
   * Coarsen the time to the same level we get on Windows -
   * 10 microseconds.
   */
  if (!NaClHighResolutionTimerEnabled()) {
    now.nacl_abi_tv_usec = (now.nacl_abi_tv_usec / 10) * 10;
  }
#endif
  CHECK(now.nacl_abi_tv_usec >= 0);
  CHECK(now.nacl_abi_tv_usec < NACL_MICROS_PER_UNIT);
  if (!NaClCopyOutToUser(natp->nap, (uintptr_t) tv, &now, sizeof now)) {
    return -NACL_ABI_EFAULT;
  }
  return 0;
}

static int NaClIsValidClockId(int clk_id) {
  switch (clk_id) {
    case NACL_ABI_CLOCK_REALTIME:
    case NACL_ABI_CLOCK_MONOTONIC:
    case NACL_ABI_CLOCK_PROCESS_CPUTIME_ID:
    case NACL_ABI_CLOCK_THREAD_CPUTIME_ID:
      return 1;
  }
  return 0;
}

int32_t NaClSysClockGetCommon(struct NaClAppThread  *natp,
                              int                   clk_id,
                              uint32_t              ts_addr,
                              int                   (*timefunc)(
                                  nacl_clockid_t            clk_id,
                                  struct nacl_abi_timespec  *tp)) {
  struct NaClApp            *nap = natp->nap;
  int                       retval = -NACL_ABI_EINVAL;
  struct nacl_abi_timespec  out_buf;

  if (!NaClIsValidClockId(clk_id)) {
    goto done;
  }
  retval = (*timefunc)((nacl_clockid_t) clk_id, &out_buf);
  if (0 == retval) {
    if (!NaClCopyOutToUser(nap, (uintptr_t) ts_addr,
                           &out_buf, sizeof out_buf)) {
      retval = -NACL_ABI_EFAULT;
    }
  }
 done:
  return retval;
}

int32_t NaClSysClockGetRes(struct NaClAppThread *natp,
                           int                  clk_id,
                           uint32_t             tsp) {
  return NaClSysClockGetCommon(natp, clk_id, (uintptr_t) tsp,
                                     NaClClockGetRes);
}

int32_t NaClSysClockGetTime(struct NaClAppThread  *natp,
                            int                   clk_id,
                            uint32_t              tsp) {
  return NaClSysClockGetCommon(natp, clk_id, (uintptr_t) tsp,
                                     NaClClockGetTime);
}
