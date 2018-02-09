#ifndef __MOCHA_RPC_CHANNEL_C_INTERNAL_H__
#define __MOCHA_RPC_CHANNEL_C_INTERNAL_H__
#include <mocha/rpc-c/RPCChannel.h>
#include <mocha/rpc/RPCChannel.h>
#include "RPCLite.h"

using namespace mocha::rpc;

struct MochaRPCChannel : MochaRPCWrapper
{
  RPCChannel *impl;
  volatile MochaRPCEventListener listener;
  MochaRPCOpaqueData userData;
  int32_t eventMask;
};

#endif /* __MOCHA_RPC_CHANNEL_C_INTERNAL_H__ */
