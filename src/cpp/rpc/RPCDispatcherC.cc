#include "mocha/rpc-c/RPCDispatcher.h"
#include "mocha/rpc/RPCDispatcher.h"

using namespace mocha::rpc;

#define CAST_BUILDER_TO(x) reinterpret_cast<MochaRPCDispatcherBuilder *>(x)
#define CAST_BUILDER_FROM(x) reinterpret_cast<RPCDispatcher::Builder *>(x)
#define CAST_DISPATCHER_TO(x) reinterpret_cast<MochaRPCDispatcher *>(x)
#define CAST_DISPATCHER_FROM(x) reinterpret_cast<RPCDispatcher *>(x)
#define CAST_DISPATCHER_THREAD_TO(x) reinterpret_cast<MochaRPCDispatcherThread *>(x)
#define CAST_DISPATCHER_THREAD_FROM(x) reinterpret_cast<RPCDispatcherThread *>(x)

MochaRPCDispatcherBuilder *
MochaRPCDispatcherBuilderCreate(void)
{
  return CAST_BUILDER_TO(RPCDispatcher::newBuilder());
}

MochaRPCDispatcherBuilder *
MochaRPCDispatcherBuilderLogger(MochaRPCDispatcherBuilder *builder, MochaRPCLogger logger, MochaRPCLogLevel level, MochaRPCOpaqueData loggerUserData)
{
  CAST_BUILDER_FROM(builder)->logger(reinterpret_cast<RPCLogger>(logger), static_cast<RPCLogLevel>(level), loggerUserData);
  return builder;
}

int32_t
MochaRPCDispatcherBuilderBuild(MochaRPCDispatcherBuilder *builder, MochaRPCDispatcher **dispatcher)
{
  *dispatcher = CAST_DISPATCHER_TO(CAST_BUILDER_FROM(builder)->build());
  if (*dispatcher) {
    return MOCHA_RPC_OK;
  } else {
    return MOCHA_RPC_INTERNAL_ERROR;
  }
}

void
MochaRPCDispatcherBuilderDestroy(MochaRPCDispatcherBuilder *builder)
{
  delete CAST_BUILDER_FROM(builder);
}

void
MochaRPCDispatcherAddRef(MochaRPCDispatcher *dispatcher)
{
  CAST_DISPATCHER_FROM(dispatcher)->addRef();
}

int32_t
MochaRPCDispatcherRelease(MochaRPCDispatcher *dispatcher)
{
  return CAST_DISPATCHER_FROM(dispatcher)->release();
}

int32_t
MochaRPCDispatcherStop(MochaRPCDispatcher *dispatcher)
{
  return CAST_DISPATCHER_FROM(dispatcher)->stop();
}

int32_t
MochaRPCDispatcherRun(MochaRPCDispatcher *dispatcher, int32_t flags)
{
  return CAST_DISPATCHER_FROM(dispatcher)->run(flags);
}

int32_t
MochaRPCDispatcherIsDispatchingThread(MochaRPCDispatcher *dispatcher)
{
  return CAST_DISPATCHER_FROM(dispatcher)->isDispatchingThread();
}

MochaRPCDispatcherThread *
MochaRPCDispatcherThreadCreate(MochaRPCDispatcher *dispatcher)
{
  return CAST_DISPATCHER_THREAD_TO(RPCDispatcherThread::create(CAST_DISPATCHER_FROM(dispatcher)));
}

int32_t
MochaRPCDispatcherThreadShutdown(MochaRPCDispatcherThread *thread)
{
  return CAST_DISPATCHER_THREAD_FROM(thread)->shutdown();
}

int32_t
MochaRPCDispatcherThreadInterrupt(MochaRPCDispatcherThread *thread)
{
  return CAST_DISPATCHER_THREAD_FROM(thread)->interrupt();
}

int32_t
MochaRPCDispatcherThreadJoin(MochaRPCDispatcherThread *thread)
{
  return CAST_DISPATCHER_THREAD_FROM(thread)->join();
}

void
MochaRPCDispatcherThreadAddRef(MochaRPCDispatcherThread *thread)
{
  CAST_DISPATCHER_THREAD_FROM(thread)->addRef();
}

int32_t
MochaRPCDispatcherThreadRelease(MochaRPCDispatcherThread *thread)
{
  return CAST_DISPATCHER_THREAD_FROM(thread)->release();
}
