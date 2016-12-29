#ifndef __MOCA_RPC_DECLARATION_H__
#define __MOCA_RPC_DECLARATION_H__ 1

#include <new>
#include <moca/rpc-c/RPCDecl.h>

#define MOCA_RPC_NAMESPACE ::moca::rpc

#define BEGIN_MOCA_RPC_NAMESPACE namespace moca { namespace rpc {
#define END_MOCA_RPC_NAMESPACE }}

BEGIN_MOCA_RPC_NAMESPACE
using namespace std;

typedef enum MocaRPCStatus RPCStatus;

#define RPC_OK (MOCA_RPC_OK)
#define RPC_INVALID_ARGUMENT (MOCA_RPC_INVALID_ARGUMENT)
#define RPC_CORRUPTED_DATA (MOCA_RPC_CORRUPTED_DATA)
#define RPC_OUT_OF_MEMORY (MOCA_RPC_OUT_OF_MEMORY)
#define RPC_CAN_NOT_CONNECT (MOCA_RPC_CAN_NOT_CONNECT)
#define RPC_NO_ACCESS (MOCA_RPC_NO_ACCESS)
#define RPC_NOT_SUPPORTED (MOCA_RPC_NOT_SUPPORTED)
#define RPC_TOO_MANY_OPEN_FILE (MOCA_RPC_TOO_MANY_OPEN_FILE)
#define RPC_INSUFFICIENT_RESOURCE (MOCA_RPC_INSUFFICIENT_RESOURCE)
#define RPC_INTERNAL_ERROR (MOCA_RPC_INTERNAL_ERROR)
#define RPC_ILLEGAL_STATE (MOCA_RPC_ILLEGAL_STATE)
#define RPC_TIMEOUT (MOCA_RPC_TIMEOUT)
#define RPC_DISCONNECTED (MOCA_RPC_DISCONNECTED)
#define RPC_WOULDBLOCK (MOCA_RPC_WOULDBLOCK)
#define RPC_INCOMPATIBLE_PROTOCOL (MOCA_RPC_INCOMPATIBLE_PROTOCOL)
#define RPC_CAN_NOT_BIND (MOCA_RPC_CAN_NOT_BIND)
#define RPC_BUFFER_OVERFLOW (MOCA_RPC_BUFFER_OVERFLOW)
#define RPC_CANCELED (MOCA_RPC_CANCELED)

template<typename T>
T safeRead(const T &input)
{
  T result;
  MOCA_SAFE_POINTER_READ(result, &input, T)
  return result;
}

template<typename T>
T safeRead(const T *addr)
{
  T result;
  MOCA_SAFE_POINTER_READ(result, addr, T)
  return result;
}

template<typename T>
T safeRead(const void *addr)
{
  T result;
  MOCA_SAFE_POINTER_READ(result, addr, T)
  return result;
}

template<typename T>
void safeWrite(const T& data, T *addr)
{
  MOCA_SAFE_POINTER_WRITE(data, addr, T)
}

END_MOCA_RPC_NAMESPACE

#endif /* __MOCA_RPC_DECLARATION_H__ */
