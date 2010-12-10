// Copyright 2010 The Native Client Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include "native_client/tests/ppapi_geturl/url_load_request.h"

#include <stdio.h>
#include <string>
#include <sstream>

#include "native_client/src/include/portability.h"
#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/tests/ppapi_geturl/module.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"

namespace {

void OpenCallback(void* user_data, int32_t pp_error) {
  UrlLoadRequest* obj = reinterpret_cast<UrlLoadRequest*>(user_data);
  if (NULL != obj)
    obj->OpenCallback(pp_error);
}

void FinishStreamingToFileCallback(void* user_data, int32_t pp_error) {
  UrlLoadRequest* obj = reinterpret_cast<UrlLoadRequest*>(user_data);
  if (NULL != obj)
    obj->FinishStreamingToFileCallback(pp_error);
}

void ReadResponseBodyCallback(void* user_data, int32_t pp_error_or_bytes) {
  UrlLoadRequest* obj = reinterpret_cast<UrlLoadRequest*>(user_data);
  if (NULL != obj)
    obj->ReadResponseBodyCallback(pp_error_or_bytes);
}

void ReadFileBodyCallback(void* user_data, int32_t pp_error) {
  UrlLoadRequest* obj = reinterpret_cast<UrlLoadRequest*>(user_data);
  if (NULL != obj)
    obj->ReadFileBodyCallback(pp_error);
}

}  // namespace


UrlLoadRequest::UrlLoadRequest(PP_Instance instance)
    : delete_this_after_report(false),
      as_file_(false),
      instance_(instance),
      request_(0),
      loader_(0),
      request_interface_(NULL),
      loader_interface_(NULL),
#if __native_client__
      fileio_interface_(NULL) {
      // TODO(polina): when proxy supports FileIO_NaCl, add it here.
#else
      fileio_interface_(NULL),
      fileio_trusted_interface_(NULL) {
#endif
}

UrlLoadRequest::~UrlLoadRequest() {
  Clear();
}

void UrlLoadRequest::Clear() {
  Module* module = Module::Get();
  if (0 != request_) {
    module->core_interface()->ReleaseResource(request_);
    request_ = 0;
  }
  if (0 != loader_) {
    module->core_interface()->ReleaseResource(loader_);
    loader_ = 0;
  }
  url_body_.clear();
}

bool UrlLoadRequest::ReportSuccess() {
  Module::Get()->ReportResult(
      instance_, url_.c_str(), as_file_, url_body_.c_str(), true);
  if (delete_this_after_report) {
    delete this;
  }
  return true;
}

bool UrlLoadRequest::ReportFailure(const std::string& error) {
  Module::Get()->ReportResult(
      instance_, url_.c_str(), as_file_, error.c_str(), false);
  if (delete_this_after_report) {
    delete this;
  }
  return false;
}

bool UrlLoadRequest::ReportFailure(const std::string& message,
                                   int32_t pp_error) {
  std::string error = message;
  error.append(Module::ErrorCodeToStr(pp_error));
  return ReportFailure(error);
}

bool UrlLoadRequest::Load(bool as_file, std::string url) {
  printf("--- UrlLoadRequest::Load(as_file=%d, '%s')\n", as_file, url.c_str());
  url_ = url;
  as_file_ = as_file;
  Clear();
  std::string error;
  if (!GetRequiredInterfaces(&error)) {
    return ReportFailure(error);
  }
  PP_Bool set_url = request_interface_->SetProperty(
      request_, PP_URLREQUESTPROPERTY_URL, Module::StrToVar(url));
  PP_Bool set_method = request_interface_->SetProperty(
      request_, PP_URLREQUESTPROPERTY_METHOD, Module::StrToVar("GET"));
  PP_Bool pp_as_file = as_file ? PP_TRUE : PP_FALSE;
  PP_Bool set_file = request_interface_->SetProperty(
      request_, PP_URLREQUESTPROPERTY_STREAMTOFILE, PP_MakeBool(pp_as_file));
  if (set_url != PP_TRUE || set_method != PP_TRUE || set_file != PP_TRUE) {
    return ReportFailure("PPB_URLRequestInfo::SetProperty: failed");
  }
  int32_t pp_error = loader_interface_->Open(
      loader_,
      request_,
      PP_MakeCompletionCallback(::OpenCallback, this));
  CHECK(pp_error != PP_OK);  // Open() never succeeds synchronously.
  if (pp_error != PP_ERROR_WOULDBLOCK) {  // Asynch failure.
    return ReportFailure("PPB_URLLoader::Open: ", pp_error);
  }
  return true;
}

bool UrlLoadRequest::GetRequiredInterfaces(std::string* error) {
  Module* module = Module::Get();

  request_interface_ = static_cast<const PPB_URLRequestInfo*>(
      module->GetBrowserInterface(PPB_URLREQUESTINFO_INTERFACE));
  if (NULL == request_interface_) {
    *error = "Failed to get browser interface '" PPB_URLREQUESTINFO_INTERFACE;
    return false;
  }
  request_ = request_interface_->Create(module->module_id());
  if (0 == request_) {
    *error = "PPB_URLRequestInfo::Create: failed";
    return false;
  }

  response_interface_ = static_cast<const PPB_URLResponseInfo*>(
      module->GetBrowserInterface(PPB_URLRESPONSEINFO_INTERFACE));
  if (NULL == response_interface_) {
    *error = "Failed to get browser interface '" PPB_URLRESPONSEINFO_INTERFACE;
    return false;
  }

  loader_interface_ = static_cast<const PPB_URLLoader*>(
          module->GetBrowserInterface(PPB_URLLOADER_INTERFACE));
  if (NULL == loader_interface_) {
    *error = "Failed to get browser interface '" PPB_URLLOADER_INTERFACE;
    return false;
  }
  loader_ = loader_interface_->Create(instance_);
  if (0 == loader_) {
    *error = "PPB_URLLoader::Create: failed";
    return false;
  }

  fileio_interface_ = static_cast<const PPB_FileIO_Dev*>(
      module->GetBrowserInterface(PPB_FILEIO_DEV_INTERFACE));
  if (NULL == fileio_interface_) {
    *error = "Failed to get browser interface '" PPB_FILEIO_DEV_INTERFACE;
    return false;
  }
  fileio_ = fileio_interface_->Create(module->module_id());
  if (0 == fileio_) {
    *error = "PPB_FileIO_Dev::Create: failed";
    return false;
  }

#if __native_client__
  NACL_UNIMPLEMENTED();  // TODO(polina): support FileIO_NaCl interface
#else
  fileio_trusted_interface_ = static_cast<const PPB_FileIOTrusted_Dev*>(
      module->GetBrowserInterface(PPB_FILEIOTRUSTED_DEV_INTERFACE));
  if (NULL == fileio_trusted_interface_) {
    *error = "Failed to get browser interface '";
    error->append(PPB_FILEIOTRUSTED_DEV_INTERFACE);
    return false;
  }
#endif

  return true;
}

void UrlLoadRequest::ReadResponseBody() {
  int32_t pp_error_or_bytes = loader_interface_->ReadResponseBody(
      loader_,
      buffer_,
      sizeof(buffer_),
      PP_MakeCompletionCallback(::ReadResponseBodyCallback, this));
  if (pp_error_or_bytes >= PP_OK) {  // Synchronous read, callback ignored.
    ReadResponseBodyCallback(pp_error_or_bytes);
  } else if (pp_error_or_bytes != PP_ERROR_WOULDBLOCK) {  // Asynch failure.
    ReportFailure("PPB_URLLoader::ReadResponseBody: ", pp_error_or_bytes);
  }
}

void UrlLoadRequest::ReadFileBody() {
  PP_Resource fileref = response_interface_->GetBodyAsFileRef(response_);
  if (0 == fileref) {
    ReportFailure("UrlLoadRequest::ReadFileBody: null file");
    return;
  }

  int32_t pp_error = fileio_interface_->Open(
      fileio_,
      fileref,
      PP_FILEOPENFLAG_READ,
      PP_MakeCompletionCallback(::ReadFileBodyCallback, this));
  CHECK(pp_error != PP_OK);  // Open() never succeeds synchronously.
  if (pp_error != PP_ERROR_WOULDBLOCK)  {  // Async failure.
    ReportFailure("PPB_FileIO_Dev::Open: ", pp_error);
  }
}

void UrlLoadRequest::OpenCallback(int32_t pp_error) {
  printf("--- UrlLoadRequest::OpenCallback\n");
  if (pp_error != PP_OK) {
    ReportFailure("UrlLoadRequest::OpenCallback: ", pp_error);
    return;
  }

  // Validating response headers to confirm successful loading.
  response_ = loader_interface_->GetResponseInfo(loader_);
  if (0 == response_) {
    ReportFailure("UrlLoadRequest::OpenCallback: null response");
    return;
  }
  PP_Var url = response_interface_->GetProperty(response_,
                                                PP_URLRESPONSEPROPERTY_URL);
  if (url.type != PP_VARTYPE_STRING) {
    ReportFailure("URLLoadRequest::OpenCallback: bad url type");
    return;
  }
  url_ = Module::VarToStr(url);  // Update url to be fully qualified.
  PP_Var status_code =
      response_interface_->GetProperty(response_,
                                       PP_URLRESPONSEPROPERTY_STATUSCODE);
  int32_t status_code_as_int = status_code.value.as_int;
  if (status_code_as_int != 200) {  // Not HTTP OK.
    std::stringstream error;
    error << "OpenCallback: status_code=" << status_code_as_int;
    ReportFailure(error.str());
    return;
  }

  if (as_file_) {
    int32_t pp_error = loader_interface_->FinishStreamingToFile(
        loader_,
        PP_MakeCompletionCallback(::FinishStreamingToFileCallback, this));
    if (pp_error == PP_OK) {  // Reached EOF.
      FinishStreamingToFileCallback(pp_error);
    } else if (pp_error != PP_ERROR_WOULDBLOCK) {  // Asynch failure.
      ReportFailure("PPB_URLLoader::FinishStreamingToFile: ", pp_error);
    }
  } else {
    ReadResponseBody();
  }
}

void UrlLoadRequest::FinishStreamingToFileCallback(int32_t pp_error) {
  printf("--- UrlLoadRequest::FinishStreamingToFileCallback\n");
  if (pp_error != PP_OK) {
    ReportFailure("UrlLoadRequest::FinishStreamingToFileCallback: ", pp_error);
    return;
  }
  ReadFileBody();
}

void UrlLoadRequest::ReadResponseBodyCallback(int32_t pp_error_or_bytes) {
  printf("--- UrlLoadRequest::ReadResponseBodyCallback\n");
  if (pp_error_or_bytes < PP_OK) {
    ReportFailure("UrlLoadRequest::ReadResponseBodyCallback: ",
                  pp_error_or_bytes);
  } else if (pp_error_or_bytes == PP_OK) {  // Reached EOF.
    ReportSuccess();
  } else {  // Partial read, so copy out the buffer and continue reading.
    for (int32_t i = 0; i < pp_error_or_bytes; i++)
      url_body_.push_back(buffer_[i]);
    ReadResponseBody();
  }
}

void UrlLoadRequest::ReadFileBodyCallback(int32_t pp_error) {
  printf("--- UrlLoadRequest::ReadFileBodyCallback\n");
  if (pp_error != PP_OK) {
    ReportFailure("UrlLoadRequest::ReadFileBodyCallback: ", pp_error);
    return;
  }

#if __native_client__
  NACL_UNIMPLEMENTED();  // No support for FileIO_NaCl::GetOSFileDescriptor.
#elif NACL_WINDOWS
  NACL_UNIMPLEMENTED();  // No support for Windows handles.
#else

  int32_t file_desc = fileio_trusted_interface_->GetOSFileDescriptor(fileio_);
  FILE* file = fdopen(file_desc, "rb");
  CHECK(file != NULL);
  size_t byte_count = fread(buffer_, sizeof(char), sizeof(buffer_), file);
  CHECK(byte_count != 0);
  CHECK(ferror(file) == 0);
  url_body_ = buffer_;
  ReportSuccess();
  // We leak the file_stream, but that's ok since this is just a test.
#endif
}
