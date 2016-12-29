#ifndef __MOCA_RPC_C_SERVER_H__
#define __MOCA_RPC_C_SERVER_H__ 1
#include <moca/rpc-c/RPC.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MocaRPCChannel MocaRPCChannel;
typedef struct MocaRPCServer MocaRPCServer;
typedef struct MocaRPCServerBuilder MocaRPCServerBuilder;
typedef struct MocaRPCDispatcher MocaRPCDispatcher;

typedef void (*MocaRPCServerEventListener)(MocaRPCServer *server, MocaRPCChannel *channel, int32_t eventType, MocaRPCOpaqueData eventData, MocaRPCOpaqueData userData);

MocaRPCServerBuilder *MocaRPCServerBuilderCreate(void);
MocaRPCServerBuilder *MocaRPCServerBuilderBind(MocaRPCServerBuilder *builder, const char *address);
MocaRPCServerBuilder *MocaRPCServerBuilderTimeout(MocaRPCServerBuilder *builder, int64_t timeout);
MocaRPCServerBuilder *MocaRPCServerBuilderHeaderLimit(MocaRPCServerBuilder *builder, int32_t size);
MocaRPCServerBuilder *MocaRPCServerBuilderPayloadLimit(MocaRPCServerBuilder *builder, int32_t size);
MocaRPCServerBuilder *MocaRPCServerBuilderListener(MocaRPCServerBuilder *builder, MocaRPCServerEventListener listener, MocaRPCOpaqueData userData, int32_t eventMask);
MocaRPCServerBuilder *MocaRPCServerBuilderKeepalive(MocaRPCServerBuilder *builder, int64_t interval);
MocaRPCServerBuilder *MocaRPCServerBuilderId(MocaRPCServerBuilder *builder, const char *id);
MocaRPCServerBuilder *MocaRPCServerBuilderLogger(MocaRPCServerBuilder *builder, MocaRPCLogger logger, MocaRPCLogLevel level, MocaRPCOpaqueData userData);
MocaRPCServerBuilder *MocaRPCServerBuilderFlags(MocaRPCServerBuilder *builder, int32_t flags);
MocaRPCServerBuilder *MocaRPCServerBuilderDispatcher(MocaRPCServerBuilder *builder, MocaRPCDispatcher *dispatcher);
MocaRPCServerBuilder *MocaRPCServerBuilderAttachment(MocaRPCServerBuilder *builder, MocaRPCOpaqueData attachment, MocaRPCOpaqueDataDestructor attachmentDestructor);

int32_t MocaRPCServerBuilderBuild(MocaRPCServerBuilder *builder, MocaRPCServer **server);
void MocaRPCServerBuilderDestroy(MocaRPCServerBuilder *builder);

void MocaRPCServerAddRef(MocaRPCServer *server);
int32_t MocaRPCServerRelease(MocaRPCServer *server);
void MocaRPCServerShutdown(MocaRPCServer *server);

void MocaRPCServerSetAttachment(MocaRPCServer *server, MocaRPCOpaqueData attachment, MocaRPCOpaqueDataDestructor destructor);
MocaRPCOpaqueData MocaRPCServerGetAttachment(MocaRPCServer *server);

#ifdef __cplusplus
}
#endif

#endif
