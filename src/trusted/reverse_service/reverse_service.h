/* -*- c++ -*- */
/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef NATIVE_CLIENT_SRC_TRUSTED_REVERSE_SERVICE_REVERSE_SERVICE_H_
#define NATIVE_CLIENT_SRC_TRUSTED_REVERSE_SERVICE_REVERSE_SERVICE_H_

#include <set>

#include "native_client/src/include/nacl_compiler_annotations.h"
#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/include/nacl_scoped_ptr.h"
#include "native_client/src/include/nacl_string.h"

#include "native_client/src/public/name_service.h"
#include "native_client/src/shared/platform/refcount_base.h"
#include "native_client/src/shared/platform/nacl_sync.h"
#include "native_client/src/trusted/desc/nacl_desc_wrapper.h"
#include "native_client/src/trusted/reverse_service/reverse_service_c.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"

struct NaClFileInfo;

namespace nacl {

class ReverseInterface : public RefCountBase {
 public:
  virtual ~ReverseInterface() {}

  // Startup handshake
  virtual void StartupInitializationComplete() = 0;

  // Name service use.
  //
  // Some of these functions require that the actual operation be done
  // in a different thread, so that the implementation of the
  // interface will have to block the requesting thread.  However, on
  // surf away, the thread switch may get cancelled, and the
  // implementation will have to reply with a failure indication.

  // The bool functions returns false if the service thread unblocked
  // because of surf-away, shutdown, or other issues.  The plugin,
  // when it tells sel_ldr to shut down, will also signal all threads
  // that are waiting for main thread callbacks to wake up and abandon
  // their vigil after the callbacks are all cancelled (by abandoning
  // the WeakRefAnchor or by bombing their CompletionCallbackFactory).
  // Since shutdown/surfaway is the only admissible error, we use bool
  // as the return type.
  virtual bool OpenManifestEntry(nacl::string url_key,
                                 struct NaClFileInfo* info) = 0;
  virtual void ReportCrash() = 0;

  // The low-order 8 bits of the |exit_status| should be reported to
  // any interested parties.
  virtual void ReportExitStatus(int exit_status) = 0;

  // Quota checking for files that were sent to the untrusted module.
  // TODO(sehr): remove the stub interface once the plugin provides one.
  virtual int64_t RequestQuotaForWrite(nacl::string file_id,
                                       int64_t offset,
                                       int64_t bytes_to_write) {
    UNREFERENCED_PARAMETER(file_id);
    UNREFERENCED_PARAMETER(offset);
    return bytes_to_write;
  }

  // covariant impl of Ref()
  ReverseInterface* Ref() {  // down_cast
    return reinterpret_cast<ReverseInterface*>(RefCountBase::Ref());
  }
};

class ReverseService : public RefCountBase {
 public:
  ReverseService(DescWrapper* conn_cap, ReverseInterface* rif);

  // covariant impl of Ref()
  ReverseService* Ref() {  // down_cast
    return reinterpret_cast<ReverseService*>(RefCountBase::Ref());
  }

  // Start starts the reverse service by initiating a connection on
  // the conn_cap and spawning a service thread using the
  // ReverseInterface rif, both provided in the ctor.
  //
  // If |crash_report| is true, then the ReportCrash virtual function
  // will be invoked when the reverse channel is closed.  Typically
  // this is needed only in one (the "primary" or "bootstrap")
  // instance of the reverse service, since additional channels
  // created are often used for and are under application program
  // control, and the untrusted application should be able to close
  // those channels without generating a false crash report.
  bool Start(bool crash_report);

  bool Start() {
    return Start(true);
  }

  void WaitForServiceThreadsToExit();

  void IncrThreadCount();
  void DecrThreadCount();

  ReverseInterface* reverse_interface() const { return reverse_interface_; }

 protected:
  ~ReverseService();

 private:
  NACL_DISALLOW_COPY_AND_ASSIGN(ReverseService);

  NaClReverseService* service_;
  ReverseInterface* reverse_interface_;
};

}  // namespace nacl

#endif
