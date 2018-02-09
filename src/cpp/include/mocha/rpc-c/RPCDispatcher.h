#ifndef __MOCHA_RPC_C_DISPATCHER_H__
#define __MOCHA_RPC_C_DISPATCHER_H__ 1
#include <mocha/rpc-c/RPC.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MochaRPCDispatcherFlags {
  MOCHA_RPC_DISPATCHER_RUN_FLAG_DEFAULT = 0,
  MOCHA_RPC_DISPATCHER_RUN_FLAG_ONE_SHOT = 1,
  MOCHA_RPC_DISPATCHER_RUN_FLAG_NONBLOCK = 2,
} MochaRPCDispatcherFlags;

typedef struct MochaRPCDispatcher MochaRPCDispatcher;
typedef struct MochaRPCDispatcherBuilder MochaRPCDispatcherBuilder;
typedef struct MochaRPCDispatcherThread MochaRPCDispatcherThread;

MochaRPCDispatcherBuilder *MochaRPCDispatcherBuilderCreate(void);
MochaRPCDispatcherBuilder *MochaRPCDispatcherBuilderLogger(MochaRPCDispatcherBuilder *builder, MochaRPCLogger logger, MochaRPCLogLevel level, MochaRPCOpaqueData userData);
int32_t MochaRPCDispatcherBuilderBuild(MochaRPCDispatcherBuilder *builder, MochaRPCDispatcher **dispatcher);
void MochaRPCDispatcherBuilderDestroy(MochaRPCDispatcherBuilder *builder);

void MochaRPCDispatcherAddRef(MochaRPCDispatcher *dispatcher);
int32_t MochaRPCDispatcherRelease(MochaRPCDispatcher *dispatcher);
int32_t MochaRPCDispatcherStop(MochaRPCDispatcher *dispatcher);
int32_t MochaRPCDispatcherRun(MochaRPCDispatcher *dispatcher, int32_t flags);
int32_t MochaRPCDispatcherIsDispatchingThread(MochaRPCDispatcher *dispatcher);

MochaRPCDispatcherThread *MochaRPCDispatcherThreadCreate(MochaRPCDispatcher *dispatcher);
int32_t MochaRPCDispatcherThreadShutdown(MochaRPCDispatcherThread *thread);
int32_t MochaRPCDispatcherThreadInterrupt(MochaRPCDispatcherThread *thread);
int32_t MochaRPCDispatcherThreadJoin(MochaRPCDispatcherThread *thread);

void MochaRPCDispatcherThreadAddRef(MochaRPCDispatcherThread *thread);
int32_t MochaRPCDispatcherThreadRelease(MochaRPCDispatcherThread *thread);

#ifdef __cplusplus
}
#endif

#endif
