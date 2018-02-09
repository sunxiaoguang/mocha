#ifndef __MOCHA_RPC_C_CHANNEL_H__
#define __MOCHA_RPC_C_CHANNEL_H__ 1
#include <mocha/rpc-c/RPC.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MochaRPCChannel MochaRPCChannel;
typedef struct MochaRPCChannelBuilder MochaRPCChannelBuilder;
typedef struct MochaRPCDispatcher MochaRPCDispatcher;

typedef enum MochaRPCChannelFlags {
  MOCHA_RPC_CHANNEL_FLAG_DEFAULT = 0,
  MOCHA_RPC_CHANNEL_FLAG_BLOCKING = 1,
  MOCHA_RPC_CHANNEL_FLAG_BUFFERED_PAYLOAD = 2,
} MochaRPCChannelFlags;

typedef void (*MochaRPCEventListener)(MochaRPCChannel *channel, int32_t eventType, MochaRPCOpaqueData eventData, MochaRPCOpaqueData userData);

MochaRPCChannelBuilder *MochaRPCChannelBuilderCreate(void);
MochaRPCChannelBuilder *MochaRPCChannelBuilderBind(MochaRPCChannelBuilder *builder, const char *address);
MochaRPCChannelBuilder *MochaRPCChannelBuilderConnect(MochaRPCChannelBuilder *builder, const char *address);
MochaRPCChannelBuilder *MochaRPCChannelBuilderTimeout(MochaRPCChannelBuilder *builder, int64_t timeout);
MochaRPCChannelBuilder *MochaRPCChannelBuilderLimit(MochaRPCChannelBuilder *builder, int32_t size);
MochaRPCChannelBuilder *MochaRPCChannelBuilderListener(MochaRPCChannelBuilder *builder, MochaRPCEventListener listener, MochaRPCOpaqueData userData, int32_t eventMask);
MochaRPCChannelBuilder *MochaRPCChannelBuilderKeepalive(MochaRPCChannelBuilder *builder, int64_t interval);
MochaRPCChannelBuilder *MochaRPCChannelBuilderId(MochaRPCChannelBuilder *builder, const char *id);
MochaRPCChannelBuilder *MochaRPCChannelBuilderLogger(MochaRPCChannelBuilder *builder, MochaRPCLogger logger, MochaRPCLogLevel level, MochaRPCOpaqueData userData);
MochaRPCChannelBuilder *MochaRPCChannelBuilderFlags(MochaRPCChannelBuilder *builder, int32_t flags);
MochaRPCChannelBuilder *MochaRPCChannelBuilderDispatcher(MochaRPCChannelBuilder *builder, MochaRPCDispatcher *dispatcher);
MochaRPCChannelBuilder *MochaRPCChannelBuilderAttachment(MochaRPCChannelBuilder *builder, MochaRPCOpaqueData attachment, MochaRPCOpaqueDataDestructor attachmentDestructor);

int32_t MochaRPCChannelBuilderBuild(MochaRPCChannelBuilder *builder, MochaRPCChannel **channel);
void MochaRPCChannelBuilderDestroy(MochaRPCChannelBuilder *builder);

void MochaRPCChannelAddRef(MochaRPCChannel *channel);
int32_t MochaRPCChannelRelease(MochaRPCChannel *channel);
void MochaRPCChannelClose(MochaRPCChannel *channel);

int32_t MochaRPCChannelLocalAddress(MochaRPCChannel *channel, char **localAddress, uint16_t *port);
int32_t MochaRPCChannelRemoteAddress(MochaRPCChannel *channel, char **remoteAddress, uint16_t *port);
int32_t MochaRPCChannelLocalId(MochaRPCChannel *channel, char **localId);
int32_t MochaRPCChannelRemoteId(MochaRPCChannel *channel, char **remoteId);
int32_t MochaRPCChannelResponse(MochaRPCChannel *channel, int64_t id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MochaRPCChannelRequest(MochaRPCChannel *channel, int64_t *id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
int32_t MochaRPCChannelRequest2(MochaRPCChannel *channel, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize);
void MochaRPCChannelSetAttachment(MochaRPCChannel *channel, MochaRPCOpaqueData attachment, MochaRPCOpaqueDataDestructor destructor);
MochaRPCOpaqueData MochaRPCChannelGetAttachment(MochaRPCChannel *channel);

#ifdef __cplusplus
}
#endif

#endif
