#include "moca/rpc-c/RPCDispatcher.h"
#include "moca/rpc/RPCDispatcher.h"

using namespace moca::rpc;

#define CAST_BUILDER_TO(x) reinterpret_cast<MocaRPCDispatcherBuilder *>(x)
#define CAST_BUILDER_FROM(x) reinterpret_cast<RPCDispatcher::Builder *>(x)
#define CAST_DISPATCHER_TO(x) reinterpret_cast<MocaRPCDispatcher *>(x)
#define CAST_DISPATCHER_FROM(x) reinterpret_cast<RPCDispatcher *>(x)
#define CAST_DISPATCHER_THREAD_TO(x) reinterpret_cast<MocaRPCDispatcherThread *>(x)
#define CAST_DISPATCHER_THREAD_FROM(x) reinterpret_cast<RPCDispatcherThread *>(x)

MocaRPCDispatcherBuilder *
MocaRPCDispatcherBuilderCreate(void)
{
  return CAST_BUILDER_TO(RPCDispatcher::newBuilder());
}

MocaRPCDispatcherBuilder *
MocaRPCDispatcherBuilderLogger(MocaRPCDispatcherBuilder *builder, MocaRPCLogger logger, MocaRPCLogLevel level, MocaRPCOpaqueData loggerUserData)
{
  CAST_BUILDER_FROM(builder)->logger(reinterpret_cast<RPCLogger>(logger), static_cast<RPCLogLevel>(level), loggerUserData);
  return builder;
}

int32_t
MocaRPCDispatcherBuilderBuild(MocaRPCDispatcherBuilder *builder, MocaRPCDispatcher **dispatcher)
{
  *dispatcher = CAST_DISPATCHER_TO(CAST_BUILDER_FROM(builder)->build());
  if (*dispatcher) {
    return MOCA_RPC_OK;
  } else {
    return MOCA_RPC_INTERNAL_ERROR;
  }
}

void
MocaRPCDispatcherBuilderDestroy(MocaRPCDispatcherBuilder *builder)
{
  delete CAST_BUILDER_FROM(builder);
}

void
MocaRPCDispatcherAddRef(MocaRPCDispatcher *dispatcher)
{
  CAST_DISPATCHER_FROM(dispatcher)->addRef();
}

int32_t
MocaRPCDispatcherRelease(MocaRPCDispatcher *dispatcher)
{
  return CAST_DISPATCHER_FROM(dispatcher)->release();
}

int32_t
MocaRPCDispatcherStop(MocaRPCDispatcher *dispatcher)
{
  return CAST_DISPATCHER_FROM(dispatcher)->stop();
}

int32_t
MocaRPCDispatcherRun(MocaRPCDispatcher *dispatcher, int32_t flags)
{
  return CAST_DISPATCHER_FROM(dispatcher)->run(flags);
}

int32_t
MocaRPCDispatcherIsDispatchingThread(MocaRPCDispatcher *dispatcher)
{
  return CAST_DISPATCHER_FROM(dispatcher)->isDispatchingThread();
}

MocaRPCDispatcherThread *
MocaRPCDispatcherThreadCreate(MocaRPCDispatcher *dispatcher)
{
  return CAST_DISPATCHER_THREAD_TO(RPCDispatcherThread::create(CAST_DISPATCHER_FROM(dispatcher)));
}

int32_t
MocaRPCDispatcherThreadShutdown(MocaRPCDispatcherThread *thread)
{
  return CAST_DISPATCHER_THREAD_FROM(thread)->shutdown();
}

int32_t
MocaRPCDispatcherThreadInterrupt(MocaRPCDispatcherThread *thread)
{
  return CAST_DISPATCHER_THREAD_FROM(thread)->interrupt();
}

int32_t
MocaRPCDispatcherThreadJoin(MocaRPCDispatcherThread *thread)
{
  return CAST_DISPATCHER_THREAD_FROM(thread)->join();
}

void
MocaRPCDispatcherThreadAddRef(MocaRPCDispatcherThread *thread)
{
  CAST_DISPATCHER_THREAD_FROM(thread)->addRef();
}

int32_t
MocaRPCDispatcherThreadRelease(MocaRPCDispatcherThread *thread)
{
  return CAST_DISPATCHER_THREAD_FROM(thread)->release();
}
