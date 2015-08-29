#include "RPC.h"

BEGIN_MOCA_RPC_NAMESPACE

volatile LogLevel defaultLoggerLogLevel_;

const char *errorString(int32_t code)
{
  switch (code) {
    case RPC_OK:
      return "Ok";
    case RPC_INVALID_ARGUMENT:
      return "Invalid argument";
    case RPC_CORRUPTED_DATA:
      return "Corrupted data";
    case RPC_OUT_OF_MEMORY:
      return "Running out of memory";
    case RPC_CAN_NOT_CONNECT:
      return "Can not connect to remote server";
    case RPC_NO_ACCESS:
      return "No access";
    case RPC_NOT_SUPPORTED:
      return "Not supported";
    case RPC_TOO_MANY_OPEN_FILE:
      return "Too many open files";
    case RPC_INSUFFICIENT_RESOURCE:
      return "Insufficient resource";
    case RPC_INTERNAL_ERROR:
      return "Internal error";
    case RPC_ILLEGAL_STATE:
      return "Illegal state";
    case RPC_TIMEOUT:
      return "Socket timeout";
    default:
      return "Unknown Error";
  }
}

END_MOCA_RPC_NAMESPACE
