#ifndef __MOCA_RPC_CHANNEL_C_INTERNAL_H__
#define __MOCA_RPC_CHANNEL_C_INTERNAL_H__
#include <moca/rpc-c/RPCChannel.h>
#include <moca/rpc/RPCChannel.h>
#include "RPCLite.h"

using namespace moca::rpc;

struct MocaRPCChannel : MocaRPCWrapper
{
  RPCChannel *impl;
  volatile MocaRPCEventListener listener;
  MocaRPCOpaqueData userData;
  int32_t eventMask;
};

#endif /* __MOCA_RPC_CHANNEL_C_INTERNAL_H__ */
