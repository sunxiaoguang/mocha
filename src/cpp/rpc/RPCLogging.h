#ifndef __MOCHA_RPC_LOGGING_H__
#define __MOCHA_RPC_LOGGING_H__ 1

#include "mocha/rpc/RPC.h"

BEGIN_MOCHA_RPC_NAMESPACE

#define DEFAULT_LOG_LEVEL RPC_LOG_LEVEL_INFO
//extern volatile RPCLogLevel defaultRPCSimpleLoggerLogLevel;
//extern RPCSimpleLoggerSink *defaultRPCSimpleLoggerSink;


extern RPCLogger defaultRPCLogger;
extern volatile RPCLogLevel defaultRPCLoggerLevel;
extern RPCOpaqueData defaultRPCLoggerUserData;


END_MOCHA_RPC_NAMESPACE

#define LOGGER_TRACE_ENABLED(level) (level <= RPC_LOG_LEVEL_TRACE)
#define LOGGER_DEBUG_ENABLED(level) (level <= RPC_LOG_LEVEL_DEBUG)
#define LOGGER_INFO_ENABLED(level) (level <= RPC_LOG_LEVEL_INFO)
#define LOGGER_WARN_ENABLED(level) (level <= RPC_LOG_LEVEL_WARN)
#define LOGGER_ERROR_ENABLED(level) (level <= RPC_LOG_LEVEL_ERROR)
#define LOGGER_FATAL_ENABLED(level) (level <= RPC_LOG_LEVEL_FATAL)

#define LOGGER_TRACE(logger, level, userData, ...)                                      \
  if (LOGGER_TRACE_ENABLED(level)) {                                                    \
    logger(RPC_LOG_LEVEL_TRACE, userData, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);   \
  }

#define LOGGER_DEBUG(logger, level, userData, ...)                                      \
  if (LOGGER_DEBUG_ENABLED(level)) {                                                    \
    logger(RPC_LOG_LEVEL_DEBUG, userData, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);   \
  }

#define LOGGER_INFO(logger, level, userData, ...)                                       \
  if (LOGGER_INFO_ENABLED(level)) {                                                     \
    logger(RPC_LOG_LEVEL_INFO, userData, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);    \
  }

#define LOGGER_WARN(logger, level, userData, ...)                                       \
  if (LOGGER_WARN_ENABLED(level)) {                                                     \
    logger(RPC_LOG_LEVEL_WARN, userData, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);    \
  }

#define LOGGER_ERROR(logger, level, userData, ...)                                      \
  if (LOGGER_ERROR_ENABLED(level)) {                                                    \
    logger(RPC_LOG_LEVEL_ERROR, userData, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);   \
  }

#define LOGGER_FATAL(logger, level, userData, ...)                                      \
  if (LOGGER_FATAL_ENABLED(level)) {                                                    \
    logger(RPC_LOG_LEVEL_FATAL, userData, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);   \
  }

#define LOGGER_ASSERT(logger, level, userData, ...)                                     \
  if (LOGGER_ASSERT_ENABLED(level)) {                                                   \
    logger(RPC_LOG_LEVEL_ASSERT, userData, __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__);  \
  }

#define LOGGER_TRACE_AT(logger, level, userData, func, file, line, ...)                 \
  if (LOGGER_TRACE_ENABLED(level)) {                                                    \
    logger(RPC_LOG_LEVEL_TRACE, userData, func, file, line, __VA_ARGS__);                   \
  }

#define LOGGER_DEBUG_AT(logger, level, userData, func, file, line, ...)                 \
  if (LOGGER_DEBUG_ENABLED(level)) {                                                    \
    logger(RPC_LOG_LEVEL_DEBUG, userData, func, file, line, __VA_ARGS__);                   \
  }

#define LOGGER_INFO_AT(logger, level, userData, func, file, line, ...)                  \
  if (LOGGER_INFO_ENABLED(level)) {                                                     \
    logger(RPC_LOG_LEVEL_INFO, userData, func, file, line, __VA_ARGS__);                    \
  }

#define LOGGER_WARN_AT(logger, level, userData, func, file, line, ...)                  \
  if (LOGGER_WARN_ENABLED(level)) {                                                     \
    logger(RPC_LOG_LEVEL_WARN, userData, func, file, line, __VA_ARGS__);                    \
  }

#define LOGGER_ERROR_AT(logger, level, userData, func, file, line, ...)                 \
  if (LOGGER_ERROR_ENABLED(level)) {                                                    \
    logger(RPC_LOG_LEVEL_ERROR, userData, func, file, line, __VA_ARGS__);                   \
  }

#define LOGGER_FATAL_AT(logger, level, userData, func, file, line, ...)                 \
  if (LOGGER_FATAL_ENABLED(level)) {                                                    \
    logger(RPC_LOG_LEVEL_FATAL, userData, func, file, line, __VA_ARGS__);                   \
  }

#define LOGGER_ASSERT_AT(logger, level, userData, func, file, line, ...)                \
  if (LOGGER_ASSERT_ENABLED(level)) {                                                   \
    logger(RPC_LOG_LEVEL_ASSERT, userData, func, file, line, __VA_ARGS__);                  \
  }

#define RPC_LOG_TRACE(...) LOGGER_TRACE(logger_, level_, loggerUserData_, __VA_ARGS__)
#define RPC_LOG_DEBUG(...) LOGGER_DEBUG(logger_, level_, loggerUserData_, __VA_ARGS__)
#define RPC_LOG_INFO(...) LOGGER_INFO(logger_, level_, loggerUserData_, __VA_ARGS__)
#define RPC_LOG_WARN(...) LOGGER_WARN(logger_, level_, loggerUserData_, __VA_ARGS__)
#define RPC_LOG_ERROR(...) LOGGER_ERROR(logger_, level_, loggerUserData_, __VA_ARGS__)
#define RPC_LOG_FATAL(...) LOGGER_FATAL(logger_, level_, loggerUserData_, __VA_ARGS__)
#define RPC_LOG_ASSERT(...) LOGGER_ASSERT(logger_, level_, loggerUserData_, __VA_ARGS__)

#endif
