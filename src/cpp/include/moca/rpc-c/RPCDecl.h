#ifndef __MOCA_RPC_C_DECLARATION_H__
#define __MOCA_RPC_C_DECLARATION_H__ 1

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOCA_RPC_FAILED(st)   ((st) != RPC_OK)

#define MOCA_RPC_CHECK_NOT_NULL(arg, error)   \
  do {                                        \
    if ((arg) == NULL) {                      \
      return error;                           \
    }                                         \
  } while (0);

#define MOCA_RPC_CHECK_ARGUMENT(arg) MOCA_RPC_CHECK_NOT_NULL(arg, MOCA_RPC_INVALID_ARGUMENT)
#define MOCA_RPC_CHECK_MEMORY(arg) MOCA_RPC_CHECK_NOT_NULL(arg, MOCA_RPC_OUT_OF_MEMORY)

#define MOCA_RPC_DO(EXPRESSION)   \
  do {                            \
    int32_t code = (EXPRESSION);  \
    if (MOCA_RPC_FAILED(code)) {  \
      return code;                \
    }                             \
  } while (0);

#define MOCA_RPC_DO_GOTO(CODE, EXPRESSION, GOTO)  \
  do {                                            \
    if (MOCA_RPC_FAILED(CODE = (EXPRESSION))) {   \
      goto GOTO;                                  \
    }                                             \
  } while (0);

#define MOCA_RPC_ALIGN(size, align) (((size) + (align) - 1) & ~((align) - 1))
#define MOCA_RPC_ALIGN_IS_ALIGN(offset, align) (((offset) & ((align) - 1)) == 0)

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
    if (MOCA_RPC_ALIGN_IS_ALIGN((size_t) ((uintptr_t) address), sizeof(type))) {        \
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
    if (MOCA_RPC_ALIGN_IS_ALIGN((size_t) ((uintptr_t) address), sizeof(type))) {        \
      (value) = *(const type *) address;                                            \
    } else {                                                                        \
      memcpy(&tmp, address, sizeof(type));                                          \
      (value) = tmp;                                                                \
    }                                                                               \
  } while (0);
#endif

typedef enum MocaRPCStatus {
  MOCA_RPC_OK = 0,
  MOCA_RPC_INVALID_ARGUMENT = -1,
  MOCA_RPC_CORRUPTED_DATA = -2,
  MOCA_RPC_OUT_OF_MEMORY = -3,
  MOCA_RPC_CAN_NOT_CONNECT = -4,
  MOCA_RPC_NO_ACCESS = -5,
  MOCA_RPC_NOT_SUPPORTED = -6,
  MOCA_RPC_TOO_MANY_OPEN_FILE = -7,
  MOCA_RPC_INSUFFICIENT_RESOURCE = -8,
  MOCA_RPC_INTERNAL_ERROR = -9,
  MOCA_RPC_ILLEGAL_STATE = -10,
  MOCA_RPC_TIMEOUT = -11,
  MOCA_RPC_DISCONNECTED = -12,
  MOCA_RPC_WOULDBLOCK = -13,
  MOCA_RPC_INCOMPATIBLE_PROTOCOL = -14,
  MOCA_RPC_CAN_NOT_BIND = -15,
  MOCA_RPC_BUFFER_OVERFLOW = -16,
  MOCA_RPC_CANCELED = -17,
} MocaRPCStatus;

#ifdef __cplusplus
}
#endif

#endif /* __MOCA_RPC_C_DECLARATION_H__ */
