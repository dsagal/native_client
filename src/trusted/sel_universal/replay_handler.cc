// Copyright (c) 2011 The Native Client Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Note: this file implements a simple replay engine for srpcs.
//       For simplicity this replay engine contains
//       some global data structures but since we only
//       expect to have a single NaClCommandLoop instance that
//       should not matter.
#include "native_client/src/include/portability.h"
#include "native_client/src/shared/srpc/nacl_srpc.h"
#include "native_client/src/shared/platform/nacl_log.h"
#include "native_client/src/trusted/sel_universal/parsing.h"
#include "native_client/src/trusted/sel_universal/replay_handler.h"
#include "native_client/src/trusted/sel_universal/rpc_universal.h"


#include <stdio.h>
#include <iterator>
#include <algorithm>
#include <string>
#include <vector>
#include <set>

using std::string;
using std::vector;
using std::set;

namespace {


struct ReplayItem {
  int count;
  string signature;
  vector<string> args_in;
  vector<string> args_out;
};

// List of all "canned" rpcs, order is important
vector<ReplayItem*> GlobalReplayList;

// ugly hack to have access to the NaClCommandLoop even when processing an rpc
NaClCommandLoop* GlobalCommandLoop;

bool RpcMatchesReplayItem(string signature,
                          NaClSrpcArg** inputs,
                          ReplayItem* ri) {
  UNREFERENCED_PARAMETER(inputs);
  if (ri->signature != signature) return false;
  if (ri->count == 0 ) return false;

  NaClLog(2, "found potential match for %s\n", signature.c_str());

  // now for the more costly parameter comparsion
  // Build the input parameter values.
  const size_t n = ri->args_in.size();
  NaClSrpcArg  in[NACL_SRPC_MAX_ARGS];
  NaClSrpcArg* inv[NACL_SRPC_MAX_ARGS + 1];
  BuildArgVec(inv, in, n);
  if (!ParseArgs(inv,  ri->args_in, 0, true, GlobalCommandLoop)) {
    // TODO(sehr): reclaim memory here on failure.
    NaClLog(LOG_ERROR, "Bad input args for RPC.\n");
    return false;
  }

  const bool result = AllArgsEqual(inv, inputs);
  FreeArrayArgs(inv);
  return result;
}


void ReplayRpc(NaClSrpcRpc* rpc,
               NaClSrpcArg** inputs,
               NaClSrpcArg** outputs,
               NaClSrpcClosure* done) {
  const char* rpc_name;
  const char* arg_types;
  const char* ret_types;

  if (!NaClSrpcServiceMethodNameAndTypes(rpc->channel->server,
                                        rpc->rpc_number,
                                        &rpc_name,
                                        &arg_types,
                                        &ret_types)) {
    NaClLog(LOG_ERROR, "cannot find signature for rpc %d\n", rpc->rpc_number);
    rpc->result = NACL_SRPC_RESULT_APP_ERROR;
    done->Run(done);
    return;
  }

  NaClLog(1, "attempt to replay: %s (%s) -> %s\n",
          rpc_name, arg_types, ret_types);

  string signature = string(rpc_name) + ":" +  arg_types + ":" + ret_types;
  for (size_t i = 0; i < GlobalReplayList.size(); ++i) {
    if (!RpcMatchesReplayItem(signature, inputs, GlobalReplayList[i])) {
      continue;
    }
    NaClLog(1, "found replay rpc\n");
    if (!ParseArgs(outputs,
                   GlobalReplayList[i]->args_out,
                   0,
                   true,
                   GlobalCommandLoop)) {
      NaClLog(LOG_ERROR, "Bad input args for RPC.\n");
      break;
    }

    NaClLog(1, "sending %d result values\n",
            (int) GlobalReplayList[i]->args_out.size());
    DumpArgs(outputs, GlobalCommandLoop);
    fflush(stdout);

    rpc->result = NACL_SRPC_RESULT_OK;
    NaClLog(1, "invoke callback\n");
    --GlobalReplayList[i]->count;
    done->Run(done);
    return;
  }

  NaClLog(LOG_WARNING, "No replay rpc found for rpc %s, args:\n", rpc_name);
  DumpArgs(inputs, GlobalCommandLoop);
  printf("\n");
  fflush(stdout);

  // if we exit the for loop here we have an error
  rpc->result = NACL_SRPC_RESULT_APP_ERROR;
  done->Run(done);
}
}  // end namespace


bool HandlerReplay(NaClCommandLoop* ncl, const vector<string>& args) {
  UNREFERENCED_PARAMETER(ncl);
  // we need three args at start and the "*" in out separator
  if (args.size() < 4) {
    NaClLog(LOG_ERROR, "Insufficient arguments to 'rpc' command.\n");
    return false;
  }

  // NOTE: small leak here
  ReplayItem* ri = new ReplayItem;
  ri->count = strtoul(args[1].c_str(), 0, 0);
  ri->signature = args[2];

  // TODO(robertm): use stl find
  size_t in_out_sep;
  for (in_out_sep = 3; in_out_sep < args.size(); ++in_out_sep) {
    if (args[in_out_sep] ==  "*")
      break;
  }

  if (in_out_sep == args.size()) {
    NaClLog(LOG_ERROR, "Missing input/output argument separator\n");
    return false;
  }

  copy(args.begin() + 3,
       args.begin() + in_out_sep,
       back_inserter(ri->args_in));
  copy(args.begin() + in_out_sep + 1,
       args.end(),
       back_inserter(ri->args_out));

  GlobalReplayList.push_back(ri);
  return true;
}


bool HandlerReplayActivate(NaClCommandLoop* ncl, const vector<string>& args) {
  if (args.size() != 1) {
    NaClLog(LOG_ERROR, "not the right number of args for this command\n");
    return false;
  }

  set<string> sigs;
  for (size_t i = 0; i < GlobalReplayList.size(); ++i) {
    sigs.insert(GlobalReplayList[i]->signature);
  }

  for (set<string>::iterator it = sigs.begin(); it != sigs.end(); ++it) {
    ncl->AddUpcallRpc(*it, ReplayRpc);
  }
  // ugly hack
  GlobalCommandLoop = ncl;
  return true;
}
