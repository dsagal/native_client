// Copyright (c) 2010 The Native Client Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "native_client/src/shared/ppapi_proxy/plugin_resource_tracker.h"

#include <limits>
#include <set>

#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/include/portability.h"
#include "native_client/src/shared/ppapi_proxy/plugin_globals.h"
#include "native_client/src/shared/ppapi_proxy/plugin_resource.h"
#include "ppapi/c/pp_resource.h"
#include "srpcgen/ppb_rpc.h"

namespace ppapi_proxy {

PluginResourceTracker::ResourceAndRefCounts::ResourceAndRefCounts(
    PluginResource* r) : resource(r), browser_refcount(1), plugin_refcount(1) {
}

PluginResourceTracker::ResourceAndRefCounts::~ResourceAndRefCounts() {
}

scoped_refptr<PluginResource> PluginResourceTracker::GetExistingResource(
    PP_Resource res) const {
  ResourceMap::const_iterator result = live_resources_.find(res);
  if (result == live_resources_.end())
    return scoped_refptr<PluginResource>();
  else
    return result->second.resource;
}

PluginResourceTracker::PluginResourceTracker() : last_id_(0) {
}

void PluginResourceTracker::AddResource(PluginResource* resource,
                                        PP_Resource id) {
  // Add the resource with plugin use-count 1.
  live_resources_.insert(std::make_pair(id, ResourceAndRefCounts(resource)));
}

bool PluginResourceTracker::AddRefResource(PP_Resource res) {
  ResourceMap::iterator i = live_resources_.find(res);
  if (i == live_resources_.end()) {
    return false;
  } else {
    // We don't protect against overflow, since a plugin as malicious as to ref
    // once per every byte in the address space could have just as well unrefed
    // one time too many.
    i->second.plugin_refcount++;
    return true;
  }
}

bool PluginResourceTracker::UnrefResource(PP_Resource res) {
  ResourceMap::iterator i = live_resources_.find(res);
  if (i != live_resources_.end()) {
    i->second.plugin_refcount--;
    if (0 == i->second.plugin_refcount) {
      size_t browser_refcount = i->second.browser_refcount;
      i->second.resource->StoppedTracking();
      live_resources_.erase(i);

      // Release all browser references.
      ReleaseBrowserResource(res, browser_refcount);
    }
    return true;
  } else {
    return false;
  }
}

void PluginResourceTracker::ObtainBrowserResource(PP_Resource res) {
  if (res) {
    NaClSrpcChannel* channel = ppapi_proxy::GetMainSrpcChannel();
    PpbCoreRpcClient::PPB_Core_AddRefResource(channel, res);
  }
}

void PluginResourceTracker::ReleaseBrowserResource(PP_Resource res,
                                                   size_t browser_refcount) {
  // Release all browser references.
  if (res) {
    NaClSrpcChannel* channel = ppapi_proxy::GetMainSrpcChannel();
    PpbCoreRpcClient::ReleaseResourceMultipleTimes(channel, res,
                                                   browser_refcount);
  }
}

}  // namespace ppapi_proxy

