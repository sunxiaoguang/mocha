#ifndef __MOCHA_RPC_C_DECLARATION_H__
#define __MOCHA_RPC_C_DECLARATION_H__ 1

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOCHA_RPC_FAILED(st)   ((st) != RPC_OK)

#define MOCHA_RPC_CHECK_NOT_NULL(arg, error)   \
  do {                                        \
    if ((arg) == NULL) {                      \
      return error;                           \
    }                                         \
  } while (0);

#define MOCHA_RPC_CHECK_ARGUMENT(arg) MOCHA_RPC_CHECK_NOT_NULL(arg, MOCHA_RPC_INVALID_ARGUMENT)
#define MOCHA_RPC_CHECK_MEMORY(arg) MOCHA_RPC_CHECK_NOT_NULL(arg, MOCHA_RPC_OUT_OF_MEMORY)

#define MOCHA_RPC_DO(EXPRESSION)   \
  do {                            \
    int32_t code = (EXPRESSION);  \
    if (MOCHA_RPC_FAILED(code)) {  \
      return code;                \
    }                             \
  } while (0);

#define MOCHA_RPC_DO_GOTO(CODE, EXPRESSION, GOTO)  \
  do {                                            \
    if (MOCHA_RPC_FAILED(CODE = (EXPRESSION))) {   \
      goto GOTO;                                  \
    }                                             \
  } while (0);

#define MOCHA_RPC_ALIGN(size, align) (((size) + (align) - 1) & ~((align) - 1))
#define MOCHA_RPC_ALIGN_IS_ALIGN(offset, align) (((offset) & ((align) - 1)) == 0)

#ifdef __i386__
#define MOCHA_SAFE_POINTER_WRITE(value, address, type)                               \
  do {                                                                              \
    *(type *) (address) = (value);                                                  \
  } while (0);
#define MOCHA_SAFE_POINTER_READ(value, address, type)                                \
  do {                                                                              \
    (value) = *(const type *) address;                                              \
  } while (0);
#else
#define MOCHA_SAFE_POINTER_WRITE(value, address1, type)                              \
  do {                                                                              \
    uint8_t *address = (uint8_t *) (address1);                                      \
    type tmp;                                                                       \
    if (MOCHA_RPC_ALIGN_IS_ALIGN((size_t) ((uintptr_t) address), sizeof(type))) {        \
      *(type *) address = (value);                                                  \
    } else {                                                                        \
      tmp = (value);                                                                \
      memcpy(address, &tmp, sizeof(type));                                          \
    }                                                                               \
  } while (0);
#define MOCHA_SAFE_POINTER_READ(value, address1, type)                               \
  do {                                                                              \
    const uint8_t *address = (const uint8_t *) (address1);                          \
    type tmp;                                                                       \
    if (MOCHA_RPC_ALIGN_IS_ALIGN((size_t) ((uintptr_t) address), sizeof(type))) {        \
      (value) = *(const type *) address;                                            \
    } else {                                                                        \
      memcpy(&tmp, address, sizeof(type));                                          \
      (value) = tmp;                                                                \
    }                                                                               \
  } while (0);
#endif

typedef enum MochaRPCStatus {
  MOCHA_RPC_OK = 0,
  MOCHA_RPC_INVALID_ARGUMENT = -1,
  MOCHA_RPC_CORRUPTED_DATA = -2,
  MOCHA_RPC_OUT_OF_MEMORY = -3,
  MOCHA_RPC_CAN_NOT_CONNECT = -4,
  MOCHA_RPC_NO_ACCESS = -5,
  MOCHA_RPC_NOT_SUPPORTED = -6,
  MOCHA_RPC_TOO_MANY_OPEN_FILE = -7,
  MOCHA_RPC_INSUFFICIENT_RESOURCE = -8,
  MOCHA_RPC_INTERNAL_ERROR = -9,
  MOCHA_RPC_ILLEGAL_STATE = -10,
  MOCHA_RPC_TIMEOUT = -11,
  MOCHA_RPC_DISCONNECTED = -12,
  MOCHA_RPC_WOULDBLOCK = -13,
  MOCHA_RPC_INCOMPATIBLE_PROTOCOL = -14,
  MOCHA_RPC_CAN_NOT_BIND = -15,
  MOCHA_RPC_BUFFER_OVERFLOW = -16,
  MOCHA_RPC_CANCELED = -17,
} MochaRPCStatus;

#ifdef __cplusplus
}
#endif

#endif /* __MOCHA_RPC_C_DECLARATION_H__ */
