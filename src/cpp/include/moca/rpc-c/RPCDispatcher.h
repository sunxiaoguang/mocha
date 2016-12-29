#ifndef __MOCA_RPC_C_DISPATCHER_H__
#define __MOCA_RPC_C_DISPATCHER_H__ 1
#include <moca/rpc-c/RPC.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MocaRPCDispatcherFlags {
  MOCA_RPC_DISPATCHER_RUN_FLAG_DEFAULT = 0,
  MOCA_RPC_DISPATCHER_RUN_FLAG_ONE_SHOT = 1,
  MOCA_RPC_DISPATCHER_RUN_FLAG_NONBLOCK = 2,
} MocaRPCDispatcherFlags;

typedef struct MocaRPCDispatcher MocaRPCDispatcher;
typedef struct MocaRPCDispatcherBuilder MocaRPCDispatcherBuilder;
typedef struct MocaRPCDispatcherThread MocaRPCDispatcherThread;

MocaRPCDispatcherBuilder *MocaRPCDispatcherBuilderCreate(void);
MocaRPCDispatcherBuilder *MocaRPCDispatcherBuilderLogger(MocaRPCDispatcherBuilder *builder, MocaRPCLogger logger, MocaRPCLogLevel level, MocaRPCOpaqueData userData);
int32_t MocaRPCDispatcherBuilderBuild(MocaRPCDispatcherBuilder *builder, MocaRPCDispatcher **dispatcher);
void MocaRPCDispatcherBuilderDestroy(MocaRPCDispatcherBuilder *builder);

void MocaRPCDispatcherAddRef(MocaRPCDispatcher *dispatcher);
int32_t MocaRPCDispatcherRelease(MocaRPCDispatcher *dispatcher);
int32_t MocaRPCDispatcherStop(MocaRPCDispatcher *dispatcher);
int32_t MocaRPCDispatcherRun(MocaRPCDispatcher *dispatcher, int32_t flags);
int32_t MocaRPCDispatcherIsDispatchingThread(MocaRPCDispatcher *dispatcher);

MocaRPCDispatcherThread *MocaRPCDispatcherThreadCreate(MocaRPCDispatcher *dispatcher);
int32_t MocaRPCDispatcherThreadShutdown(MocaRPCDispatcherThread *thread);
int32_t MocaRPCDispatcherThreadInterrupt(MocaRPCDispatcherThread *thread);
int32_t MocaRPCDispatcherThreadJoin(MocaRPCDispatcherThread *thread);

void MocaRPCDispatcherThreadAddRef(MocaRPCDispatcherThread *thread);
int32_t MocaRPCDispatcherThreadRelease(MocaRPCDispatcherThread *thread);

#ifdef __cplusplus
}
#endif

#endif
