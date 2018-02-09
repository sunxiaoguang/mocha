#ifndef __MOCHA_RPC_PROTOCOL_C_INTERNAL_H__
#define __MOCHA_RPC_PROTOCOL_C_INTERNAL_H__

#include "mocha/rpc-c/RPCChannel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MochaRPCProtocolFlags {
  MOCHA_RPC_PROTOCOL_NEGOTIATION_FLAG_ACCEPT_ZLIB = 1 << 0,
  MOCHA_RPC_PROTOCOL_NEGOTIATION_FLAG_NO_HINT = 1 << 1,
  MOCHA_RPC_PROTOCOL_NEGOTIATION_FLAG_MASK = 0xFFFF,
} MochaRPCProtocolFlags;

typedef struct MochaRPCProtocol MochaRPCProtocol;

typedef void (*MochaRPCProtocolWrite)(MochaRPCOpaqueData buffer, size_t size, MochaRPCOpaqueData userData);
typedef void (*MochaRPCProtocolListener)(int32_t eventType, MochaRPCOpaqueData eventData, MochaRPCOpaqueData userData);
typedef void (*MochaRPCProtocolShutdown)(MochaRPCOpaqueData userData);

MochaRPCProtocol *MochaRPCProtocolCreate(MochaRPCProtocolShutdown shutdown, MochaRPCProtocolListener listener, MochaRPCProtocolWrite write, MochaRPCOpaqueData userData);
MochaRPCProtocol *MochaRPCProtocolCreateNegotiated(MochaRPCProtocolShutdown shutdown, MochaRPCProtocolListener listener, MochaRPCProtocolWrite write, MochaRPCOpaqueData userData);
int32_t MochaRPCProtocolRead(MochaRPCProtocol *protocol, MochaRPCOpaqueData data, int32_t size);
int32_t MochaRPCProtocolStart(MochaRPCProtocol *protocol, const char *id, MochaRPCLogger logger, MochaRPCLogLevel level, MochaRPCOpaqueData loggerUserData, int32_t flags, int32_t limit, int32_t channelFlags);
int32_t MochaRPCProtocolKeepAlive(MochaRPCProtocol *protocol);
int32_t MochaRPCProtocolResponse(MochaRPCProtocol *protocol, int64_t id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MochaRPCProtocolRequest(MochaRPCProtocol *protocol, int64_t *id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MochaRPCProtocolRequest2(MochaRPCProtocol *protocol, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MochaRPCProtocolRequest3(MochaRPCProtocol *protocol, int64_t id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MochaRPCProtocolSendNegotiation(MochaRPCProtocol *protocol);
int32_t MochaRPCProtocolIsEstablished(MochaRPCProtocol *protocol);
int32_t MochaRPCProtocolProcessError(MochaRPCProtocol *protocol, int32_t status);
int32_t MochaRPCProtocolConnected(MochaRPCProtocol *protocol);
const char *MochaRPCProtocolLocalId(MochaRPCProtocol *protocol);
const char *MochaRPCProtocolRemoteId(MochaRPCProtocol *protocol);
void MochaRPCProtocolDestroy(MochaRPCProtocol *protocol);
int32_t MochaRPCProtocolPendingSize(MochaRPCProtocol *protocol);
MochaRPCOpaqueData MochaRPCProtocolUserData(MochaRPCProtocol *protocol);

#ifdef __cplusplus
}
#endif

#endif /* __MOCHA_RPC_PROTOCOL_C_INTERNAL_H__ */
