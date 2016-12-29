#ifndef __MOCA_RPC_C_CHANNEL_H__
#define __MOCA_RPC_C_CHANNEL_H__ 1
#include <moca/rpc-c/RPC.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MocaRPCChannel MocaRPCChannel;
typedef struct MocaRPCChannelBuilder MocaRPCChannelBuilder;
typedef struct MocaRPCDispatcher MocaRPCDispatcher;

typedef enum MocaRPCChannelFlags {
  MOCA_RPC_CHANNEL_FLAG_DEFAULT = 0,
  MOCA_RPC_CHANNEL_FLAG_BLOCKING = 1,
  MOCA_RPC_CHANNEL_FLAG_BUFFERED_PAYLOAD = 2,
} MocaRPCChannelFlags;

typedef void (*MocaRPCEventListener)(MocaRPCChannel *channel, int32_t eventType, MocaRPCOpaqueData eventData, MocaRPCOpaqueData userData);

MocaRPCChannelBuilder *MocaRPCChannelBuilderCreate(void);
MocaRPCChannelBuilder *MocaRPCChannelBuilderBind(MocaRPCChannelBuilder *builder, const char *address);
MocaRPCChannelBuilder *MocaRPCChannelBuilderConnect(MocaRPCChannelBuilder *builder, const char *address);
MocaRPCChannelBuilder *MocaRPCChannelBuilderTimeout(MocaRPCChannelBuilder *builder, int64_t timeout);
MocaRPCChannelBuilder *MocaRPCChannelBuilderLimit(MocaRPCChannelBuilder *builder, int32_t size);
MocaRPCChannelBuilder *MocaRPCChannelBuilderListener(MocaRPCChannelBuilder *builder, MocaRPCEventListener listener, MocaRPCOpaqueData userData, int32_t eventMask);
MocaRPCChannelBuilder *MocaRPCChannelBuilderKeepalive(MocaRPCChannelBuilder *builder, int64_t interval);
MocaRPCChannelBuilder *MocaRPCChannelBuilderId(MocaRPCChannelBuilder *builder, const char *id);
MocaRPCChannelBuilder *MocaRPCChannelBuilderLogger(MocaRPCChannelBuilder *builder, MocaRPCLogger logger, MocaRPCLogLevel level, MocaRPCOpaqueData userData);
MocaRPCChannelBuilder *MocaRPCChannelBuilderFlags(MocaRPCChannelBuilder *builder, int32_t flags);
MocaRPCChannelBuilder *MocaRPCChannelBuilderDispatcher(MocaRPCChannelBuilder *builder, MocaRPCDispatcher *dispatcher);
MocaRPCChannelBuilder *MocaRPCChannelBuilderAttachment(MocaRPCChannelBuilder *builder, MocaRPCOpaqueData attachment, MocaRPCOpaqueDataDestructor attachmentDestructor);

int32_t MocaRPCChannelBuilderBuild(MocaRPCChannelBuilder *builder, MocaRPCChannel **channel);
void MocaRPCChannelBuilderDestroy(MocaRPCChannelBuilder *builder);

void MocaRPCChannelAddRef(MocaRPCChannel *channel);
int32_t MocaRPCChannelRelease(MocaRPCChannel *channel);
void MocaRPCChannelClose(MocaRPCChannel *channel);

int32_t MocaRPCChannelLocalAddress(MocaRPCChannel *channel, char **localAddress, uint16_t *port);
int32_t MocaRPCChannelRemoteAddress(MocaRPCChannel *channel, char **remoteAddress, uint16_t *port);
int32_t MocaRPCChannelLocalId(MocaRPCChannel *channel, char **localId);
int32_t MocaRPCChannelRemoteId(MocaRPCChannel *channel, char **remoteId);
int32_t MocaRPCChannelResponse(MocaRPCChannel *channel, int64_t id, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MocaRPCChannelRequest(MocaRPCChannel *channel, int64_t *id, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MocaRPCChannelRequest2(MocaRPCChannel *channel, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
void MocaRPCChannelSetAttachment(MocaRPCChannel *channel, MocaRPCOpaqueData attachment, MocaRPCOpaqueDataDestructor destructor);
MocaRPCOpaqueData MocaRPCChannelGetAttachment(MocaRPCChannel *channel);

#ifdef __cplusplus
}
#endif

#endif
