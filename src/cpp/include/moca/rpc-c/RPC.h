#ifndef __MOCA_RPC_C_H__
#define __MOCA_RPC_C_H__ 1
#include <moca/rpc-c/RPCDecl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *MocaRPCOpaqueData;
typedef void (*MocaRPCOpaqueDataDestructor)(MocaRPCOpaqueData opaqueData);

typedef enum MocaRPCLogLevel {
  MOCA_RPC_LOG_LEVEL_TRACE = 0,
  MOCA_RPC_LOG_LEVEL_DEBUG,
  MOCA_RPC_LOG_LEVEL_INFO,
  MOCA_RPC_LOG_LEVEL_WARN,
  MOCA_RPC_LOG_LEVEL_ERROR,
  MOCA_RPC_LOG_LEVEL_FATAL,
  MOCA_RPC_LOG_LEVEL_ASSERT,
} MocaRPCLogLevel;

typedef void (*MocaRPCLogger)(MocaRPCLogLevel level, MocaRPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, ...);

typedef enum MocaRPCEventType {
  MOCA_RPC_EVENT_TYPE_CHANNEL_CREATED = 1 << 0,
  MOCA_RPC_EVENT_TYPE_CHANNEL_LISTENING = 1 << 1,
  MOCA_RPC_EVENT_TYPE_CHANNEL_CONNECTED = 1 << 2,
  MOCA_RPC_EVENT_TYPE_CHANNEL_ESTABLISHED = 1 << 3,
  MOCA_RPC_EVENT_TYPE_CHANNEL_DISCONNECTED = 1 << 4,
  MOCA_RPC_EVENT_TYPE_CHANNEL_REQUEST = 1 << 5,
  MOCA_RPC_EVENT_TYPE_CHANNEL_RESPONSE = 1 << 6,
  MOCA_RPC_EVENT_TYPE_CHANNEL_PAYLOAD = 1 << 7,
  MOCA_RPC_EVENT_TYPE_CHANNEL_ERROR = 1 << 8,
  MOCA_RPC_EVENT_TYPE_CHANNEL_DESTROYED = 1 << 9,
  MOCA_RPC_EVENT_TYPE_SERVER_CREATED = 1 << 29,
  MOCA_RPC_EVENT_TYPE_SERVER_DESTROYED = 1 << 30,
  MOCA_RPC_EVENT_TYPE_ALL = 0xFFFFFFFF,
} MocaRPCEventType;

typedef struct MocaRPCErrorEventData
{
  int32_t code;
  const char *message;
} MocaRPCErrorEventData;

typedef enum MocaRPCStringFlag {
  MOCA_RPC_STRING_FLAG_EMBEDDED = 1 << 0,
} MocaRPCStringFlag;

typedef struct MocaRPCString
{
  int32_t size;
  int32_t flags;
  union {
    char buffer[sizeof(size_t)];
    const char *ptr;
  } content;
} MocaRPCString;

typedef struct MocaRPCKeyValuePair
{
  MocaRPCString *key;
  MocaRPCString *value;
  uint8_t extra[sizeof(size_t)];
} MocaRPCKeyValuePair;

typedef struct MocaRPCKeyValuePairs
{
  int32_t size;
  int32_t capacity;
  MocaRPCKeyValuePair **pair;
  uint8_t extra[sizeof(size_t)];
} MocaRPCKeyValuePairs;

typedef struct MocaRPCPacketEventData
{
  int64_t id;
  int32_t code;
  int32_t payloadSize;
  MocaRPCKeyValuePairs *headers;
} MocaRPCPacketEventData;

typedef struct MocaRPCPayloadEventData
{
  int64_t id;
  int32_t code;
  int32_t size:31;
  int32_t commit:1;
  const char *payload;
} MocaRPCPayloadEventData;

typedef MocaRPCPacketEventData MocaRPCRequestEventData;
typedef MocaRPCPacketEventData MocaRPCResponseEventData;

const char *MocaRPCErrorString(int32_t code);

MocaRPCString *MocaRPCStringCreate(const char *str, int32_t length);
MocaRPCString *MocaRPCStringCreate2(const char *str);
MocaRPCString *MocaRPCStringWrap(const char *str, int32_t length);
MocaRPCString *MocaRPCStringWrap2(const char *str);
MocaRPCString *MocaRPCStringWrapTo(const char *str, int32_t length, MocaRPCString *to);
MocaRPCString *MocaRPCStringWrapTo2(const char *str, MocaRPCString *to);
const char *MocaRPCStringGet(const MocaRPCString *str);
void MocaRPCStringDestroy(MocaRPCString *string);

MocaRPCKeyValuePair *MocaRPCKeyValuePairCreate(const char *key, int32_t keyLen, const char *value, int32_t valueLen);
MocaRPCKeyValuePair *MocaRPCKeyValuePairCreate2(const char *key, const char *value);
MocaRPCKeyValuePair *MocaRPCKeyValuePairWrap(const char *key, int32_t keyLen, const char *value, int32_t valueLen);
MocaRPCKeyValuePair *MocaRPCKeyValuePairWrap2(const char *key, const char *value);
void MocaRPCKeyValuePairDestroy(MocaRPCKeyValuePair *pair);

MocaRPCKeyValuePairs *MocaRPCKeyValuePairsCreate(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize);
MocaRPCKeyValuePairs *MocaRPCKeyValuePairsCreate2(int32_t size, const char **keys, const char **values);
MocaRPCKeyValuePairs *MocaRPCKeyValuePairsWrap(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize);
MocaRPCKeyValuePairs *MocaRPCKeyValuePairsWrap2(int32_t size, const char **keys, const char **values);
MocaRPCKeyValuePairs *MocaRPCKeyValuePairsWrapTo(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize, MocaRPCKeyValuePairs *to);
MocaRPCKeyValuePairs *MocaRPCKeyValuePairsWrapTo2(int32_t size, const char **keys, const char **values, MocaRPCKeyValuePairs *to);
void MocaRPCKeyValuePairsDestroy(MocaRPCKeyValuePairs *pairs);

typedef void (*MocaRPCSimpleLoggerSinkEntry)(MocaRPCLogLevel level, MocaRPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *message);

typedef struct MocaRPCSimpleLoggerSink
{
  MocaRPCSimpleLoggerSinkEntry entry;
  MocaRPCOpaqueData userData;
} MocaRPCSimpleLoggerSink;
void MocaRPCSimpleLogger(MocaRPCLogLevel level, MocaRPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
