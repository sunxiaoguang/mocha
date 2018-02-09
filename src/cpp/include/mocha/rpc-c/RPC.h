#ifndef __MOCHA_RPC_C_H__
#define __MOCHA_RPC_C_H__ 1
#include <mocha/rpc-c/RPCDecl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *MochaRPCOpaqueData;
typedef void (*MochaRPCOpaqueDataSink)(MochaRPCOpaqueData opaqueData);
typedef MochaRPCOpaqueDataSink MochaRPCOpaqueDataDestructor;

typedef enum MochaRPCLogLevel {
  MOCHA_RPC_LOG_LEVEL_TRACE = 0,
  MOCHA_RPC_LOG_LEVEL_DEBUG,
  MOCHA_RPC_LOG_LEVEL_INFO,
  MOCHA_RPC_LOG_LEVEL_WARN,
  MOCHA_RPC_LOG_LEVEL_ERROR,
  MOCHA_RPC_LOG_LEVEL_FATAL,
  MOCHA_RPC_LOG_LEVEL_ASSERT,
} MochaRPCLogLevel;

typedef void (*MochaRPCLogger)(MochaRPCLogLevel level, MochaRPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, ...);

typedef enum MochaRPCEventType {
  MOCHA_RPC_EVENT_TYPE_CHANNEL_CREATED = 1 << 0,
  MOCHA_RPC_EVENT_TYPE_CHANNEL_LISTENING = 1 << 1,
  MOCHA_RPC_EVENT_TYPE_CHANNEL_CONNECTED = 1 << 2,
  MOCHA_RPC_EVENT_TYPE_CHANNEL_ESTABLISHED = 1 << 3,
  MOCHA_RPC_EVENT_TYPE_CHANNEL_DISCONNECTED = 1 << 4,
  MOCHA_RPC_EVENT_TYPE_CHANNEL_REQUEST = 1 << 5,
  MOCHA_RPC_EVENT_TYPE_CHANNEL_RESPONSE = 1 << 6,
  MOCHA_RPC_EVENT_TYPE_CHANNEL_PAYLOAD = 1 << 7,
  MOCHA_RPC_EVENT_TYPE_CHANNEL_ERROR = 1 << 8,
  MOCHA_RPC_EVENT_TYPE_CHANNEL_DESTROYED = 1 << 9,
  MOCHA_RPC_EVENT_TYPE_SERVER_CREATED = 1 << 29,
  MOCHA_RPC_EVENT_TYPE_SERVER_DESTROYED = 1 << 30,
  MOCHA_RPC_EVENT_TYPE_ALL = 0xFFFFFFFF,
} MochaRPCEventType;

typedef struct MochaRPCErrorEventData
{
  int32_t code;
  const char *message;
} MochaRPCErrorEventData;

typedef enum MochaRPCStringFlag {
  MOCHA_RPC_STRING_FLAG_EMBEDDED = 1 << 0,
} MochaRPCStringFlag;

typedef struct MochaRPCString
{
  int32_t size;
  int32_t flags;
  union {
    char buffer[sizeof(size_t)];
    const char *ptr;
  } content;
} MochaRPCString;

typedef struct MochaRPCKeyValuePair
{
  MochaRPCString *key;
  MochaRPCString *value;
  uint8_t extra[sizeof(size_t)];
} MochaRPCKeyValuePair;

typedef struct MochaRPCKeyValuePairs
{
  int32_t size;
  int32_t capacity;
  MochaRPCKeyValuePair **pair;
  uint8_t extra[sizeof(size_t)];
} MochaRPCKeyValuePairs;

typedef struct MochaRPCPacketEventData
{
  int64_t id;
  int32_t code;
  int32_t payloadSize;
  MochaRPCKeyValuePairs *headers;
} MochaRPCPacketEventData;

typedef struct MochaRPCPayloadEventData
{
  int64_t id;
  int32_t code;
  int32_t size:31;
  int32_t commit:1;
  const char *payload;
} MochaRPCPayloadEventData;

typedef MochaRPCPacketEventData MochaRPCRequestEventData;
typedef MochaRPCPacketEventData MochaRPCResponseEventData;

const char *MochaRPCErrorString(int32_t code);

MochaRPCString *MochaRPCStringCreate(const char *str, int32_t length);
MochaRPCString *MochaRPCStringCreate2(const char *str);
MochaRPCString *MochaRPCStringWrap(const char *str, int32_t length);
MochaRPCString *MochaRPCStringWrap2(const char *str);
MochaRPCString *MochaRPCStringWrapTo(const char *str, int32_t length, MochaRPCString *to);
MochaRPCString *MochaRPCStringWrapTo2(const char *str, MochaRPCString *to);
const char *MochaRPCStringGet(const MochaRPCString *str);
void MochaRPCStringDestroy(MochaRPCString *string);

MochaRPCKeyValuePair *MochaRPCKeyValuePairCreate(const char *key, int32_t keyLen, const char *value, int32_t valueLen);
MochaRPCKeyValuePair *MochaRPCKeyValuePairCreate2(const char *key, const char *value);
MochaRPCKeyValuePair *MochaRPCKeyValuePairWrap(const char *key, int32_t keyLen, const char *value, int32_t valueLen);
MochaRPCKeyValuePair *MochaRPCKeyValuePairWrap2(const char *key, const char *value);
void MochaRPCKeyValuePairDestroy(MochaRPCKeyValuePair *pair);

MochaRPCKeyValuePairs *MochaRPCKeyValuePairsCreate(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize);
MochaRPCKeyValuePairs *MochaRPCKeyValuePairsCreate2(int32_t size, const char **keys, const char **values);
MochaRPCKeyValuePairs *MochaRPCKeyValuePairsWrap(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize);
MochaRPCKeyValuePairs *MochaRPCKeyValuePairsWrap2(int32_t size, const char **keys, const char **values);
MochaRPCKeyValuePairs *MochaRPCKeyValuePairsWrapTo(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize, MochaRPCKeyValuePairs *to);
MochaRPCKeyValuePairs *MochaRPCKeyValuePairsWrapTo2(int32_t size, const char **keys, const char **values, MochaRPCKeyValuePairs *to);
void MochaRPCKeyValuePairsDestroy(MochaRPCKeyValuePairs *pairs);

typedef void (*MochaRPCSimpleLoggerSinkEntry)(MochaRPCLogLevel level, MochaRPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *message);

typedef struct MochaRPCSimpleLoggerSink
{
  MochaRPCSimpleLoggerSinkEntry entry;
  MochaRPCOpaqueData userData;
} MochaRPCSimpleLoggerSink;
void MochaRPCSimpleLogger(MochaRPCLogLevel level, MochaRPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
