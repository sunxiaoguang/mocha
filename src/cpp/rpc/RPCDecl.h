#ifndef __MOCA_RPC_DECLARATION_H__
#define __MOCA_RPC_DECLARATION_H__ 1

#include <stdlib.h>
#include <string.h>
#include <new>
#include <stdint.h>

#define MOCA_RPC_NAMESPACE ::moca::rpc

#define BEGIN_MOCA_RPC_NAMESPACE namespace moca { namespace rpc {
#define END_MOCA_RPC_NAMESPACE }}

#define MOCA_RPC_FAILED(st)   ((st) != RPC_OK)

#define MOCA_RPC_DO(EXPRESSION)   \
  do {                            \
    int32_t code = (EXPRESSION);  \
    if (MOCA_RPC_FAILED(code)) {  \
      return code;                \
    }                             \
  } while (false);
#define MOCA_RPC_DO_GOTO(CODE, EXPRESSION, GOTO)  \
  do {                                            \
    if (MOCA_RPC_FAILED(CODE = (EXPRESSION))) {   \
      goto GOTO;                                  \
    }                                             \
  } while (false);

#define MOCA_ALIGN(size, align) (((size) + (align) - 1) & ~((align) - 1))
#define MOCA_ALIGN_IS_ALIGN(offset, align) (((offset) & ((align) - 1)) == 0)

#ifdef __i386__
#define MOCA_SAFE_POINTER_WRITE(value, address, type)                               \
  do {                                                                              \
    *(type *) (address) = (value);                                                  \
  } while (0);
#define MOCA_SAFE_POINTER_READ(value, address, type)                                \
  do {                                                                              \
    (value) = *(const type *) address;                                              \
  } while (0);
#else
#define MOCA_SAFE_POINTER_WRITE(value, address1, type)                              \
  do {                                                                              \
    uint8_t *address = (uint8_t *) (address1);                                      \
    type tmp;                                                                       \
    if (MOCA_ALIGN_IS_ALIGN((size_t) ((uintptr_t) address), sizeof(type))) {        \
      *(type *) address = (value);                                                  \
    } else {                                                                        \
      tmp = (value);                                                                \
      memcpy(address, &tmp, sizeof(type));                                          \
    }                                                                               \
  } while (0);
#define MOCA_SAFE_POINTER_READ(value, address1, type)                               \
  do {                                                                              \
    const uint8_t *address = (const uint8_t *) (address1);                          \
    type tmp;                                                                       \
    if (MOCA_ALIGN_IS_ALIGN((size_t) ((uintptr_t) address), sizeof(type))) {        \
      (value) = *(const type *) address;                                            \
    } else {                                                                        \
      memcpy(&tmp, address, sizeof(type));                                          \
      (value) = tmp;                                                                \
    }                                                                               \
  } while (0);
#endif

BEGIN_MOCA_RPC_NAMESPACE
using namespace std;

enum {
  RPC_OK = 0,
  RPC_INVALID_ARGUMENT = -1,
  RPC_CORRUPTED_DATA = -2,
  RPC_OUT_OF_MEMORY = -3,
  RPC_CAN_NOT_CONNECT = -4,
  RPC_NO_ACCESS = -5,
  RPC_NOT_SUPPORTED = -6,
  RPC_TOO_MANY_OPEN_FILE = -7,
  RPC_INSUFFICIENT_RESOURCE = -8,
  RPC_INTERNAL_ERROR = -9,
  RPC_ILLEGAL_STATE = -10,
  RPC_TIMEOUT = -11,
  RPC_DISCONNECTED = -12,
  RPC_WOULDBLOCK = -13,
  RPC_INCOMPATIBLE_PROTOCOL = -14,
};

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
T safeRead(void *addr)
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
