#include "RPCLogging.h"
#include "RPCNano.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#define MAX_LOG_LINE_SIZE (4096)

BEGIN_MOCA_RPC_NAMESPACE

void simpleLogger(RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, va_list args)
{
  char buffer[MAX_LOG_LINE_SIZE];
  char *p = buffer;
  int32_t bufferSize = static_cast<int32_t>(sizeof(buffer));
  int32_t size;

  size = vsnprintf(p, bufferSize, fmt, args);

  if (size == 0) {
    buffer[0] = '\0';
  }

  RPCSimpleLoggerSink *sink = static_cast<RPCSimpleLoggerSink *>(userData);
  sink->entry(level, sink->userData, func, file, line, buffer);
}

void rpcSimpleLogger(RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  simpleLogger(level, userData, func, file, line, fmt, args);
  va_end(args);
}

END_MOCA_RPC_NAMESPACE

using namespace moca::rpc;

void MocaRPCSimpleLogger(MocaRPCLogLevel level, MocaRPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  simpleLogger(static_cast<RPCLogLevel>(level), userData, func, file, line, fmt, args);
  va_end(args);
}

BEGIN_MOCA_RPC_NAMESPACE

void stdoutSink(RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *message)
{
  time_t now = time(NULL);
  char timeBuffer[32];
  static char levels[] = {'T', 'D', 'I', 'W', 'E', 'F', 'A'};
  ctime_r(&now, timeBuffer);
  timeBuffer[strlen(timeBuffer) - 1] = '\0';

  printf("%s [%c:%s|%s:%u] %s\n", timeBuffer, levels[level], func, file, line, message);
}

static RPCSimpleLoggerSink simpleLoggerSink = { stdoutSink, NULL};
RPCLogger defaultRPCLogger = rpcSimpleLogger;
volatile RPCLogLevel defaultRPCLoggerLevel = DEFAULT_LOG_LEVEL;
RPCOpaqueData defaultRPCLoggerUserData = &simpleLoggerSink;

void rpcLogger(RPCLogger defaultLogger, RPCLogLevel defaultLoggerLevel, RPCOpaqueData defaultLoggerUserData)
{
  defaultRPCLoggerLevel = defaultLoggerLevel;
  defaultRPCLogger = defaultLogger;
  defaultRPCLoggerUserData = defaultLoggerUserData;
  RPC_MEMORY_BARRIER_FULL();
}

END_MOCA_RPC_NAMESPACE
