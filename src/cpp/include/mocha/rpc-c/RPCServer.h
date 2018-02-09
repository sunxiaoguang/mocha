#ifndef __MOCHA_RPC_C_SERVER_H__
#define __MOCHA_RPC_C_SERVER_H__ 1
#include <mocha/rpc-c/RPC.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MochaRPCChannel MochaRPCChannel;
typedef struct MochaRPCServer MochaRPCServer;
typedef struct MochaRPCServerBuilder MochaRPCServerBuilder;
typedef struct MochaRPCDispatcher MochaRPCDispatcher;

typedef void (*MochaRPCServerEventListener)(MochaRPCServer *server, MochaRPCChannel *channel, int32_t eventType, MochaRPCOpaqueData eventData, MochaRPCOpaqueData userData);

MochaRPCServerBuilder *MochaRPCServerBuilderCreate(void);
MochaRPCServerBuilder *MochaRPCServerBuilderBind(MochaRPCServerBuilder *builder, const char *address);
MochaRPCServerBuilder *MochaRPCServerBuilderTimeout(MochaRPCServerBuilder *builder, int64_t timeout);
MochaRPCServerBuilder *MochaRPCServerBuilderHeaderLimit(MochaRPCServerBuilder *builder, int32_t size);
MochaRPCServerBuilder *MochaRPCServerBuilderPayloadLimit(MochaRPCServerBuilder *builder, int32_t size);
MochaRPCServerBuilder *MochaRPCServerBuilderListener(MochaRPCServerBuilder *builder, MochaRPCServerEventListener listener, MochaRPCOpaqueData userData, int32_t eventMask);
MochaRPCServerBuilder *MochaRPCServerBuilderKeepalive(MochaRPCServerBuilder *builder, int64_t interval);
MochaRPCServerBuilder *MochaRPCServerBuilderId(MochaRPCServerBuilder *builder, const char *id);
MochaRPCServerBuilder *MochaRPCServerBuilderLogger(MochaRPCServerBuilder *builder, MochaRPCLogger logger, MochaRPCLogLevel level, MochaRPCOpaqueData userData);
MochaRPCServerBuilder *MochaRPCServerBuilderFlags(MochaRPCServerBuilder *builder, int32_t flags);
MochaRPCServerBuilder *MochaRPCServerBuilderDispatcher(MochaRPCServerBuilder *builder, MochaRPCDispatcher *dispatcher);
MochaRPCServerBuilder *MochaRPCServerBuilderAttachment(MochaRPCServerBuilder *builder, MochaRPCOpaqueData attachment, MochaRPCOpaqueDataDestructor attachmentDestructor);

int32_t MochaRPCServerBuilderBuild(MochaRPCServerBuilder *builder, MochaRPCServer **server);
void MochaRPCServerBuilderDestroy(MochaRPCServerBuilder *builder);

void MochaRPCServerAddRef(MochaRPCServer *server);
int32_t MochaRPCServerRelease(MochaRPCServer *server);
void MochaRPCServerShutdown(MochaRPCServer *server);

void MochaRPCServerSetAttachment(MochaRPCServer *server, MochaRPCOpaqueData attachment, MochaRPCOpaqueDataDestructor destructor);
MochaRPCOpaqueData MochaRPCServerGetAttachment(MochaRPCServer *server);

#ifdef __cplusplus
}
#endif

#endif
