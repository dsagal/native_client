// Copyright (c) 2010 The Native Client Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
//
// Automatically generated code.  See srpcgen.py
//
// NaCl Simple Remote Procedure Call interface abstractions.

#ifndef GEN_PPAPI_PROXY_PPB_RPC_H_
#define GEN_PPAPI_PROXY_PPB_RPC_H_
#ifndef __native_client__
#include "native_client/src/include/portability.h"
#endif  // __native_client__
#include "native_client/src/shared/srpc/nacl_srpc.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_resource.h"
class ObjectStubRpcClient {
 public:
  static NaClSrpcError HasProperty(
      NaClSrpcChannel* channel,
      nacl_abi_size_t capability_bytes, char* capability,
      nacl_abi_size_t name_bytes, char* name,
      nacl_abi_size_t exception_in_bytes, char* exception_in,
      int32_t* success,
      nacl_abi_size_t* exception_bytes, char* exception);
  static NaClSrpcError HasMethod(
      NaClSrpcChannel* channel,
      nacl_abi_size_t capability_bytes, char* capability,
      nacl_abi_size_t name_bytes, char* name,
      nacl_abi_size_t exception_in_bytes, char* exception_in,
      int32_t* success,
      nacl_abi_size_t* exception_bytes, char* exception);
  static NaClSrpcError GetProperty(
      NaClSrpcChannel* channel,
      nacl_abi_size_t capability_bytes, char* capability,
      nacl_abi_size_t name_bytes, char* name,
      nacl_abi_size_t exception_in_bytes, char* exception_in,
      nacl_abi_size_t* value_bytes, char* value,
      nacl_abi_size_t* exception_bytes, char* exception);
  static NaClSrpcError GetAllPropertyNames(
      NaClSrpcChannel* channel,
      nacl_abi_size_t capability_bytes, char* capability,
      nacl_abi_size_t exception_in_bytes, char* exception_in,
      int32_t* property_count,
      nacl_abi_size_t* properties_bytes, char* properties,
      nacl_abi_size_t* exception_bytes, char* exception);
  static NaClSrpcError SetProperty(
      NaClSrpcChannel* channel,
      nacl_abi_size_t capability_bytes, char* capability,
      nacl_abi_size_t name_bytes, char* name,
      nacl_abi_size_t value_bytes, char* value,
      nacl_abi_size_t exception_in_bytes, char* exception_in,
      nacl_abi_size_t* exception_bytes, char* exception);
  static NaClSrpcError RemoveProperty(
      NaClSrpcChannel* channel,
      nacl_abi_size_t capability_bytes, char* capability,
      nacl_abi_size_t name_bytes, char* name,
      nacl_abi_size_t exception_in_bytes, char* exception_in,
      nacl_abi_size_t* exception_bytes, char* exception);
  static NaClSrpcError Call(
      NaClSrpcChannel* channel,
      nacl_abi_size_t capability_bytes, char* capability,
      nacl_abi_size_t name_bytes, char* name,
      int32_t argc,
      nacl_abi_size_t argv_bytes, char* argv,
      nacl_abi_size_t exception_in_bytes, char* exception_in,
      nacl_abi_size_t* ret_bytes, char* ret,
      nacl_abi_size_t* exception_bytes, char* exception);
  static NaClSrpcError Construct(
      NaClSrpcChannel* channel,
      nacl_abi_size_t capability_bytes, char* capability,
      int32_t argc,
      nacl_abi_size_t argv_bytes, char* argv,
      nacl_abi_size_t exception_in_bytes, char* exception_in,
      nacl_abi_size_t* ret_bytes, char* ret,
      nacl_abi_size_t* exception_bytes, char* exception);
  static NaClSrpcError Deallocate(
      NaClSrpcChannel* channel,
      nacl_abi_size_t capability_bytes, char* capability);

 private:
  ObjectStubRpcClient();
  ObjectStubRpcClient(const ObjectStubRpcClient&);
  void operator=(const ObjectStubRpcClient);
};  // class ObjectStubRpcClient

class PpbRpcClient {
 public:
  static NaClSrpcError PPB_GetInterface(
      NaClSrpcChannel* channel,
      char* interface_name,
      int32_t* exports_interface_name);

 private:
  PpbRpcClient();
  PpbRpcClient(const PpbRpcClient&);
  void operator=(const PpbRpcClient);
};  // class PpbRpcClient

class PpbAudioDevRpcClient {
 public:
  static NaClSrpcError PPB_Audio_Dev_Create(
      NaClSrpcChannel* channel,
      PP_Instance instance,
      PP_Resource config,
      PP_Resource* out_resource);
  static NaClSrpcError PPB_Audio_Dev_IsAudio(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* out_bool);
  static NaClSrpcError PPB_Audio_Dev_GetCurrentConfig(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      PP_Resource* out_resource);
  static NaClSrpcError PPB_Audio_Dev_StopPlayback(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* out_bool);
  static NaClSrpcError PPB_Audio_Dev_StartPlayback(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* out_bool);

 private:
  PpbAudioDevRpcClient();
  PpbAudioDevRpcClient(const PpbAudioDevRpcClient&);
  void operator=(const PpbAudioDevRpcClient);
};  // class PpbAudioDevRpcClient

class PpbAudioConfigDevRpcClient {
 public:
  static NaClSrpcError PPB_AudioConfig_Dev_CreateStereo16Bit(
      NaClSrpcChannel* channel,
      PP_Instance instance,
      int32_t sample_rate,
      int32_t sample_frame_count,
      PP_Resource* resource);
  static NaClSrpcError PPB_AudioConfig_Dev_IsAudioConfig(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* out_bool);
  static NaClSrpcError PPB_AudioConfig_Dev_RecommendSampleFrameCount(
      NaClSrpcChannel* channel,
      int32_t request,
      int32_t* sample_frame_count);
  static NaClSrpcError PPB_AudioConfig_Dev_GetSampleRate(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* sample_rate);
  static NaClSrpcError PPB_AudioConfig_Dev_GetSampleFrameCount(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* sample_frame_count);

 private:
  PpbAudioConfigDevRpcClient();
  PpbAudioConfigDevRpcClient(const PpbAudioConfigDevRpcClient&);
  void operator=(const PpbAudioConfigDevRpcClient);
};  // class PpbAudioConfigDevRpcClient

class PpbCoreRpcClient {
 public:
  static NaClSrpcError PPB_Core_AddRefResource(
      NaClSrpcChannel* channel,
      PP_Resource resource);
  static NaClSrpcError PPB_Core_ReleaseResource(
      NaClSrpcChannel* channel,
      PP_Resource resource);
  static NaClSrpcError ReleaseResourceMultipleTimes(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t count);
  static NaClSrpcError PPB_Core_GetTime(
      NaClSrpcChannel* channel,
      double* time);

 private:
  PpbCoreRpcClient();
  PpbCoreRpcClient(const PpbCoreRpcClient&);
  void operator=(const PpbCoreRpcClient);
};  // class PpbCoreRpcClient

class PpbGraphics2DRpcClient {
 public:
  static NaClSrpcError PPB_Graphics2D_Create(
      NaClSrpcChannel* channel,
      PP_Instance instance,
      nacl_abi_size_t size_bytes, char* size,
      int32_t is_always_opaque,
      PP_Resource* resource);
  static NaClSrpcError PPB_Graphics2D_IsGraphics2D(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* success);
  static NaClSrpcError PPB_Graphics2D_Describe(
      NaClSrpcChannel* channel,
      PP_Resource graphics_2d,
      nacl_abi_size_t* size_bytes, char* size,
      int32_t* is_always_opaque,
      int32_t* success);
  static NaClSrpcError PPB_Graphics2D_PaintImageData(
      NaClSrpcChannel* channel,
      PP_Resource graphics_2d,
      PP_Resource image,
      nacl_abi_size_t top_left_bytes, char* top_left,
      nacl_abi_size_t src_rect_bytes, char* src_rect);
  static NaClSrpcError PPB_Graphics2D_Scroll(
      NaClSrpcChannel* channel,
      PP_Resource graphics_2d,
      nacl_abi_size_t clip_rect_bytes, char* clip_rect,
      nacl_abi_size_t amount_bytes, char* amount);
  static NaClSrpcError PPB_Graphics2D_ReplaceContents(
      NaClSrpcChannel* channel,
      PP_Resource graphics_2d,
      PP_Resource image);
  static NaClSrpcError PPB_Graphics2D_Flush(
      NaClSrpcChannel* channel,
      PP_Resource graphics_2d,
      int32_t callback_id,
      int32_t* pp_error);

 private:
  PpbGraphics2DRpcClient();
  PpbGraphics2DRpcClient(const PpbGraphics2DRpcClient&);
  void operator=(const PpbGraphics2DRpcClient);
};  // class PpbGraphics2DRpcClient

class PpbImageDataRpcClient {
 public:
  static NaClSrpcError PPB_ImageData_GetNativeImageDataFormat(
      NaClSrpcChannel* channel,
      int32_t* format);
  static NaClSrpcError PPB_ImageData_IsImageDataFormatSupported(
      NaClSrpcChannel* channel,
      int32_t format,
      int32_t* success);
  static NaClSrpcError PPB_ImageData_Create(
      NaClSrpcChannel* channel,
      PP_Instance instance,
      int32_t format,
      nacl_abi_size_t size_bytes, char* size,
      int32_t init_to_zero,
      PP_Resource* resource);
  static NaClSrpcError PPB_ImageData_IsImageData(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* success);
  static NaClSrpcError PPB_ImageData_Describe(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      nacl_abi_size_t* desc_bytes, char* desc,
      NaClSrpcImcDescType* shm,
      int32_t* shm_size,
      int32_t* success);

 private:
  PpbImageDataRpcClient();
  PpbImageDataRpcClient(const PpbImageDataRpcClient&);
  void operator=(const PpbImageDataRpcClient);
};  // class PpbImageDataRpcClient

class PpbInstanceRpcClient {
 public:
  static NaClSrpcError PPB_Instance_GetWindowObject(
      NaClSrpcChannel* channel,
      PP_Instance instance,
      nacl_abi_size_t* window_bytes, char* window);
  static NaClSrpcError PPB_Instance_GetOwnerElementObject(
      NaClSrpcChannel* channel,
      PP_Instance instance,
      nacl_abi_size_t* owner_bytes, char* owner);
  static NaClSrpcError PPB_Instance_BindGraphics(
      NaClSrpcChannel* channel,
      PP_Instance instance,
      PP_Resource graphics_device,
      int32_t* success);
  static NaClSrpcError PPB_Instance_IsFullFrame(
      NaClSrpcChannel* channel,
      PP_Instance instance,
      int32_t* is_full_frame);
  static NaClSrpcError PPB_Instance_ExecuteScript(
      NaClSrpcChannel* channel,
      PP_Instance instance,
      nacl_abi_size_t script_bytes, char* script,
      nacl_abi_size_t exception_in_bytes, char* exception_in,
      nacl_abi_size_t* result_bytes, char* result,
      nacl_abi_size_t* exception_bytes, char* exception);

 private:
  PpbInstanceRpcClient();
  PpbInstanceRpcClient(const PpbInstanceRpcClient&);
  void operator=(const PpbInstanceRpcClient);
};  // class PpbInstanceRpcClient

class PpbURLRequestInfoRpcClient {
 public:
  static NaClSrpcError PPB_URLRequestInfo_Create(
      NaClSrpcChannel* channel,
      PP_Module module,
      PP_Resource* resource);
  static NaClSrpcError PPB_URLRequestInfo_IsURLRequestInfo(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* is_url_request_info);
  static NaClSrpcError PPB_URLRequestInfo_SetProperty(
      NaClSrpcChannel* channel,
      PP_Resource request,
      int32_t property,
      nacl_abi_size_t value_bytes, char* value,
      int32_t* success);
  static NaClSrpcError PPB_URLRequestInfo_AppendDataToBody(
      NaClSrpcChannel* channel,
      PP_Resource request,
      nacl_abi_size_t data_bytes, char* data,
      int32_t* success);
  static NaClSrpcError PPB_URLRequestInfo_AppendFileToBody(
      NaClSrpcChannel* channel,
      PP_Resource request,
      PP_Resource file_ref,
      int64_t start_offset,
      int64_t number_of_bytes,
      double expected_last_modified_time,
      int32_t* success);

 private:
  PpbURLRequestInfoRpcClient();
  PpbURLRequestInfoRpcClient(const PpbURLRequestInfoRpcClient&);
  void operator=(const PpbURLRequestInfoRpcClient);
};  // class PpbURLRequestInfoRpcClient

class PpbURLResponseInfoRpcClient {
 public:
  static NaClSrpcError PPB_URLResponseInfo_IsURLResponseInfo(
      NaClSrpcChannel* channel,
      PP_Resource resource,
      int32_t* is_url_response_info);
  static NaClSrpcError PPB_URLResponseInfo_GetProperty(
      NaClSrpcChannel* channel,
      PP_Resource response,
      int32_t property,
      nacl_abi_size_t* value_bytes, char* value);
  static NaClSrpcError PPB_URLResponseInfo_GetBodyAsFileRef(
      NaClSrpcChannel* channel,
      PP_Resource response,
      PP_Resource* file_ref);

 private:
  PpbURLResponseInfoRpcClient();
  PpbURLResponseInfoRpcClient(const PpbURLResponseInfoRpcClient&);
  void operator=(const PpbURLResponseInfoRpcClient);
};  // class PpbURLResponseInfoRpcClient



#endif  // GEN_PPAPI_PROXY_PPB_RPC_H_

