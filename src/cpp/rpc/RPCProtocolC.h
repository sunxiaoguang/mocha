#ifndef __MOCA_RPC_PROTOCOL_C_INTERNAL_H__
#define __MOCA_RPC_PROTOCOL_C_INTERNAL_H__

#include "moca/rpc-c/RPCChannel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MocaRPCProtocol MocaRPCProtocol;

typedef void (*MocaRPCProtocolWrite)(MocaRPCOpaqueData buffer, size_t size, MocaRPCOpaqueData userData);
typedef void (*MocaRPCProtocolListener)(int32_t eventType, MocaRPCOpaqueData eventData, MocaRPCOpaqueData userData);
typedef void (*MocaRPCProtocolShutdown)(MocaRPCOpaqueData userData);

MocaRPCProtocol *MocaRPCProtocolCreate(MocaRPCProtocolShutdown shutdown, MocaRPCProtocolListener listener, MocaRPCProtocolWrite write, MocaRPCOpaqueData userData);
int32_t MocaRPCProtocolRead(MocaRPCProtocol *protocol, MocaRPCOpaqueData data, int32_t size);
int32_t MocaRPCProtocolStart(MocaRPCProtocol *protocol, const char *id, MocaRPCLogger logger, MocaRPCLogLevel level, MocaRPCOpaqueData loggerUserData, int32_t flags, int32_t limit, int32_t channelFlags);
int32_t MocaRPCProtocolKeepAlive(MocaRPCProtocol *protocol);
int32_t MocaRPCProtocolResponse(MocaRPCProtocol *protocol, int64_t id, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MocaRPCProtocolRequest(MocaRPCProtocol *protocol, int64_t *id, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MocaRPCProtocolRequest2(MocaRPCProtocol *protocol, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MocaRPCProtocolSendNegotiation(MocaRPCProtocol *protocol);
int32_t MocaRPCProtocolIsEstablished(MocaRPCProtocol *protocol);
int32_t MocaRPCProtocolProcessError(MocaRPCProtocol *protocol, int32_t status);
int32_t MocaRPCProtocolConnected(MocaRPCProtocol *protocol);
const char *MocaRPCProtocolLocalId(MocaRPCProtocol *protocol);
const char *MocaRPCProtocolRemoteId(MocaRPCProtocol *protocol);
void MocaRPCProtocolDestroy(MocaRPCProtocol *protocol);
int32_t MocaRPCProtocolPendingSize(MocaRPCProtocol *protocol);
MocaRPCOpaqueData MocaRPCProtocolUserData(MocaRPCProtocol *protocol);

#ifdef __cplusplus
}
#endif

#endif /* __MOCA_RPC_PROTOCOL_C_INTERNAL_H__ */
