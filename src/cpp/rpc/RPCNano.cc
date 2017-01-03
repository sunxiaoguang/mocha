#include "moca/rpc-c/RPC.h"
#include "RPCNano.h"
#include <stdio.h>
#include <arpa/inet.h>
#include <pthread.h>

BEGIN_MOCA_RPC_NAMESPACE
void
StringLite::resize(size_t size)
{
  if (size_ < size) {
    if (capacity_ < size + 1) {
      capacity_ += MOCA_RPC_ALIGN(size + 1, 8);
      content_ = static_cast<char *>(realloc(content_, capacity_));
    }
    memset(content_ + size_, ' ', size - size_);
  } else if (size_ > size * 2) {
    capacity_ = size;
    if (size == 0) {
      free(content_);
      content_ = NULL;
    } else {
      capacity_ += 1;
      content_ = static_cast<char *>(realloc(content_, capacity_));
    }
  }
  size_ = size;
  if (capacity_ > 0) {
    content_[size_] = '\0';
  }
}

void
StringLite::reserve(size_t capacity)
{
  if (capacity_ < capacity) {
    content_ = static_cast<char *>(realloc(content_, capacity_ = capacity));
  }
}

void
StringLite::shrink(size_t to)
{
  if (to == 0) {
    resize(to);
  } else {
    if (capacity_ > to) {
      content_ = static_cast<char *>(realloc(content_, capacity_ = to));
    }
    if (size_ > to) {
      size_ = to;
    }
  }
}

void
StringLite::append(const char *str, size_t len)
{
  if (capacity_ < size_ + len + 1) {
    capacity_ += MOCA_RPC_ALIGN(size_ + len + 1, 8);
    content_ = static_cast<char *>(realloc(content_, capacity_));
  }
  memcpy(content_ + size_, str, len);
  size_ += len;
  content_[size_] = '\0';
}

void
StringLite::assign(const char *str, size_t len)
{
  if (capacity_ < len + 1 || (content_ && capacity_ > len + 16) || content_ == NULL) {
    free(content_);
    capacity_ = MOCA_RPC_ALIGN(len + 1, 8);
    content_ = static_cast<char *>(malloc(capacity_));
  }
  memcpy(content_, str, len);
  content_[len] = '\0';
  size_ = len;
}

time_t getDeadline(int64_t timeout)
{
  time_t sec = timeout / 1000000;
  if (sec <= 0) {
    sec = 1;
  }
  return time(NULL) + sec;
}

bool isTimeout(time_t deadline)
{
  return time(NULL) >= deadline;
}

RPCObject::~RPCObject()
{
}

void uuidGenerate(StringLite *output)
{
#ifndef ANDROID
  uuid_t uuid;
  uuid_generate(uuid);
  char uuidStr[64];
  uuid_unparse(uuid, uuidStr);
  output->assign(uuidStr);
#else
  char buffer[32];
  for (int idx = 0; idx < 8; ++idx) {
    snprintf(buffer, sizeof(buffer), "%X", rand());
    output->append(buffer);
  }
#endif
}

int32_t
getInetAddressPresentation(const sockaddr_storage *addr, StringLite *address, uint16_t *port)
{
  if (address) {
    char buffer[INET6_ADDRSTRLEN];
    switch (addr->ss_family) {
      case AF_INET:
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *>(addr)->sin_addr, buffer, sizeof(buffer));
        break;
      case AF_INET6:
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr, buffer, sizeof(buffer));
        break;
      default:
        return RPC_INTERNAL_ERROR;
    }
    address->assign(buffer);
  }
  if (port) {
    switch (addr->ss_family) {
      case AF_INET:
        *port = ntohs(reinterpret_cast<const sockaddr_in *>(addr)->sin_port);
        break;
      case AF_INET6:
        *port = ntohs(reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_port);
        break;
      default:
        return RPC_INTERNAL_ERROR;
    }
  }
  return RPC_OK;
}

RPCMutex::RPCMutex()
{
  int32_t rc = pthread_mutex_init(&mutex_, NULL);
  if (rc != 0) {
    abort();
  }
}

RPCMutex::~RPCMutex()
{
  int32_t rc = pthread_mutex_destroy(&mutex_);
  if (rc != 0) {
    abort();
  }
}

void RPCMutex::lock() const
{
  pthread_mutex_lock(&mutex_);
}

void RPCMutex::unlock() const
{
  pthread_mutex_unlock(&mutex_);
}

RPCCondVar::RPCCondVar()
{
  pthread_cond_init(&cond_, NULL);
}

RPCCondVar::~RPCCondVar()
{
  pthread_cond_destroy(&cond_);
}

void
RPCCondVar::wait(const RPCMutex &mutex)
{
  pthread_cond_wait(&cond_, &mutex.mutex_);
}

void
RPCCondVar::broadcast()
{
  pthread_cond_broadcast(&cond_);
}

void
RPCCondVar::signal()
{
  pthread_cond_signal(&cond_);
}

RPCThreadLocalKey::RPCThreadLocalKey(RPCThreadLocalKey::Destructor destructor)
{
  pthread_key_create(&key_, destructor);
}

RPCThreadLocalKey::~RPCThreadLocalKey()
{
  pthread_key_delete(key_);
}

void RPCThreadLocalKey::set(void *data)
{
  pthread_setspecific(key_, data);
}

void *RPCThreadLocalKey::doGet()
{
  return pthread_getspecific(key_);
}

END_MOCA_RPC_NAMESPACE

const char *MocaRPCErrorString(int32_t code)
{
  switch (code) {
    case MOCA_RPC_OK:
      return "Ok";
    case MOCA_RPC_INVALID_ARGUMENT:
      return "Invalid argument";
    case MOCA_RPC_CORRUPTED_DATA:
      return "Corrupted data";
    case MOCA_RPC_OUT_OF_MEMORY:
      return "Running out of memory";
    case MOCA_RPC_CAN_NOT_CONNECT:
      return "Can not connect to remote server";
    case MOCA_RPC_NO_ACCESS:
      return "No access";
    case MOCA_RPC_NOT_SUPPORTED:
      return "Not supported";
    case MOCA_RPC_TOO_MANY_OPEN_FILE:
      return "Too many open files";
    case MOCA_RPC_INSUFFICIENT_RESOURCE:
      return "Insufficient resource";
    case MOCA_RPC_INTERNAL_ERROR:
      return "Internal error";
    case MOCA_RPC_ILLEGAL_STATE:
      return "Illegal state";
    case MOCA_RPC_TIMEOUT:
      return "Socket timeout";
    case MOCA_RPC_DISCONNECTED:
      return "Disconnected";
    case MOCA_RPC_WOULDBLOCK:
      return "Operation would block";
    case MOCA_RPC_INCOMPATIBLE_PROTOCOL:
      return "Incompatible protocol";
    case MOCA_RPC_CAN_NOT_BIND:
      return "Can not bind socket to given name";
    case MOCA_RPC_BUFFER_OVERFLOW:
      return "Running out of buffer";
    default:
      return "Unknown Error";
  }
}

using namespace moca::rpc;

#define MOCA_RPC_STRING_SIZE(len) (len + OFFSET_OF(MocaRPCString, content) + 1)
#define MOCA_RPC_KEY_VALUE_PAIR_SIZE(klen, vlen) (MOCA_RPC_STRING_SIZE(klen) + MOCA_RPC_STRING_SIZE(vlen) + OFFSET_OF(MocaRPCKeyValuePair, extra))

MocaRPCString *
convert(const char *str, int32_t len, MocaRPCString *output)
{
  memcpy(output->content.buffer, str, len);
  output->content.buffer[len] = '\0';
  output->flags = MOCA_RPC_STRING_FLAG_EMBEDDED;
  MOCA_SAFE_POINTER_WRITE(len, &output->size, int32_t);
  return output;
}

MocaRPCString *
MocaRPCStringCreate(const char *str, int32_t len)
{
  size_t realSize = MOCA_RPC_STRING_SIZE(len);
  return convert(str, len, static_cast<MocaRPCString *>(malloc(realSize)));
}

MocaRPCString *
MocaRPCStringCreate2(const char *str)
{
  /* we don't want to support huge string longer than 2GB */
  return MocaRPCStringCreate(str, static_cast<int32_t>(strlen(str)));
}

MocaRPCString *
MocaRPCStringWrapTo(const char *str, int32_t length, MocaRPCString *to)
{
  to->size = length;
  to->flags = 0;
  to->content.ptr = str;
  return to;
}

MocaRPCString *
MocaRPCStringWrapTo2(const char *str, MocaRPCString *to)
{
  return MocaRPCStringWrapTo(str, static_cast<int32_t>(strlen(str)), to);
}

MocaRPCString *
MocaRPCStringWrap(const char *str, int32_t length)
{
  return MocaRPCStringWrapTo(str, length, static_cast<MocaRPCString *>(malloc(sizeof(MocaRPCString))));
}

MocaRPCString *
MocaRPCStringWrap2(const char *str)
{
  /* we don't want to support huge string longer than 2GB */
  return MocaRPCStringWrap(str, static_cast<int32_t>(strlen(str)));
}

void
MocaRPCStringDestroy(MocaRPCString *string)
{
  free(string);
}

const char *
MocaRPCStringGet(const MocaRPCString *str)
{
  if (str->flags & MOCA_RPC_STRING_FLAG_EMBEDDED) {
    return str->content.buffer;
  } else {
    return str->content.ptr;
  }
}

MocaRPCKeyValuePair *
convert(const char *key, int32_t keyLen, const char *value, int32_t valueLen, MocaRPCKeyValuePair *pair)
{
  size_t keySize = MOCA_RPC_STRING_SIZE(keyLen);
  pair->key = unsafeGet<MocaRPCString>(pair->extra, 0);
  pair->value = unsafeGet<MocaRPCString>(pair->extra, keySize);
  convert(key, keyLen, pair->key);
  convert(value, valueLen, pair->value);
  return pair;
}

MocaRPCKeyValuePair *
MocaRPCKeyValuePairCreate(const char *key, int32_t keyLen, const char *value, int32_t valueLen)
{
  size_t realSize = MOCA_RPC_KEY_VALUE_PAIR_SIZE(keyLen, valueLen);
  return convert(key, keyLen, value, valueLen, static_cast<MocaRPCKeyValuePair *>(malloc(realSize)));
}

MocaRPCKeyValuePair *
MocaRPCKeyValuePairCreate2(const char *key, const char *value)
{
  return MocaRPCKeyValuePairCreate(key, static_cast<int32_t>(strlen(key)), value, static_cast<int32_t>(strlen(value)));
}

MocaRPCKeyValuePair *
MocaRPCKeyValuePairWrapTo(const char *key, int32_t keyLen, const char *value, int32_t valueLen, MocaRPCKeyValuePair *to)
{
  MocaRPCStringWrapTo(key, keyLen, to->key);
  MocaRPCStringWrapTo(value, valueLen, to->value);
  return to;
}

MocaRPCKeyValuePair *
MocaRPCKeyValuePairWrap(const char *key, int32_t keyLen, const char *value, int32_t valueLen)
{
  MocaRPCKeyValuePair *pair = static_cast<MocaRPCKeyValuePair *>(malloc(MOCA_RPC_KEY_VALUE_PAIR_SIZE(sizeof(void *), sizeof(void *))));
  pair->key = unsafeGet<MocaRPCString>(pair->extra, 0);
  pair->value = unsafeGet<MocaRPCString>(pair->extra, MOCA_RPC_STRING_SIZE(sizeof(void*)));
  return MocaRPCKeyValuePairWrapTo(key, keyLen, value, valueLen, pair);
}

MocaRPCKeyValuePair *
MocaRPCKeyValuePairWrap2(const char *key, const char *value)
{
  return MocaRPCKeyValuePairWrap(key, static_cast<int32_t>(strlen(key)), value, static_cast<int32_t>(strlen(value)));
}

void
MocaRPCKeyValuePairDestroy(MocaRPCKeyValuePair *pair)
{
  free(pair);
}

MocaRPCKeyValuePairs *
convert(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize, MocaRPCKeyValuePairs *pairs)
{
  MOCA_SAFE_POINTER_WRITE(size, &pairs->size, int32_t);
  pairs->pair = unsafeGet<MocaRPCKeyValuePair *>(pairs->extra, 0);
  size_t offset = size * sizeof(MocaRPCKeyValuePair *);
  for (int32_t idx = 0; idx < size; ++idx) {
    int32_t keyLen = keysSize[idx];
    int32_t valueLen = valuesSize[idx];
    int32_t pairSize = MOCA_RPC_KEY_VALUE_PAIR_SIZE(keyLen, valueLen);
    MocaRPCKeyValuePair *pair = unsafeGet<MocaRPCKeyValuePair>(pairs->extra, offset);
    convert(keys[idx], keyLen, values[idx], valueLen, pair);
    offset += pairSize;
    MOCA_SAFE_POINTER_WRITE(pair, pairs->pair + idx, MocaRPCKeyValuePair *);
  }

  return pairs;
}

MocaRPCKeyValuePairs *
MocaRPCKeyValuePairsCreate(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize)
{
  size_t totalSize = OFFSET_OF(MocaRPCKeyValuePairs, extra) + (size * sizeof(MocaRPCKeyValuePair *));

  for (int32_t idx = 0; idx < size; ++idx) {
    totalSize += MOCA_RPC_KEY_VALUE_PAIR_SIZE(keysSize[idx], valuesSize[idx]);
  }

  return convert(size, keys, keysSize, values, valuesSize, static_cast<MocaRPCKeyValuePairs *>(malloc(totalSize)));
}

MocaRPCKeyValuePairs *
MocaRPCKeyValuePairsCreate2(int32_t size, const char **keys, const char **values)
{
  int32_t buffer[64];
  int32_t *keysSize;
  int32_t *valuesSize;
  if (size <= 32) {
    keysSize = buffer;
  } else {
    keysSize = static_cast<int32_t *>(malloc(sizeof(int32_t) * size * 2));
  }
  valuesSize = keysSize + size;
  size_t totalSize = OFFSET_OF(MocaRPCKeyValuePairs, extra) + (size * sizeof(MocaRPCKeyValuePair *));

  for (int32_t idx = 0; idx < size; ++idx) {
    int32_t keySize = static_cast<int32_t>(strlen(keys[idx]));
    keysSize[idx] = keySize;
    int32_t valueSize = static_cast<int32_t>(strlen(values[idx]));
    valuesSize[idx] = valueSize;
    totalSize += MOCA_RPC_KEY_VALUE_PAIR_SIZE(keySize, valueSize);
  }

  return convert(size, keys, keysSize, values, valuesSize, static_cast<MocaRPCKeyValuePairs *>(malloc(totalSize)));
}

MocaRPCKeyValuePairs *
MocaRPCKeyValuePairsWrapToInternal(int32_t size, MocaRPCKeyValuePairsWrapToInternalSink sink, void *sinkUserData, MocaRPCKeyValuePairs *to)
{
  if (to == NULL || to->capacity < size || to->capacity - size > 32) {
    MocaRPCKeyValuePairsDestroy(to);
    int32_t capacity = MOCA_RPC_ALIGN(size, 8);
    size_t headerBufferOffset = OFFSET_OF(MocaRPCKeyValuePairs, extra) + (capacity * sizeof(MocaRPCKeyValuePair *));
    size_t stringBufferOffset = headerBufferOffset + (capacity * OFFSET_OF(MocaRPCKeyValuePair, extra));
    size_t totalSize = stringBufferOffset + (capacity * 2 * sizeof(MocaRPCString));

    MocaRPCKeyValuePairs *pairs = static_cast<MocaRPCKeyValuePairs *>(malloc(totalSize));
    pairs->pair = unsafeGet<MocaRPCKeyValuePair*>(pairs->extra, 0);
    pairs->capacity = capacity;
    MocaRPCString *string = unsafeGet<MocaRPCString>(pairs, stringBufferOffset);

    for (int32_t idx = 0; idx < capacity; ++idx) {
      MocaRPCKeyValuePair *pair = unsafeGet<MocaRPCKeyValuePair>(pairs, headerBufferOffset + (idx * OFFSET_OF(MocaRPCKeyValuePair, extra)));
      pairs->pair[idx] = pair;
      pair->key = string++;
      pair->value = string++;
    }
    to = pairs;
  }

  MocaRPCKeyValuePair **headers = to->pair;
  for (int32_t idx = 0; idx < size; ++idx) {
    sink(idx, headers[idx], sinkUserData);
  }
  to->size = size;

  return to;
}

struct MocaRPCKeyValuePairsWrapToSinkArgument {
  const char **keys;
  const char **values;
  const int32_t *keysSize;
  const int32_t *valuesSize;
};

void MocaRPCKeyValuePairsWrapToSink(int32_t index, MocaRPCKeyValuePair *pair, void *userData)
{
  MocaRPCKeyValuePairsWrapToSinkArgument *argument = static_cast<MocaRPCKeyValuePairsWrapToSinkArgument *>(userData);
  MocaRPCStringWrapTo(argument->keys[index], argument->keysSize[index], pair->key);
  MocaRPCStringWrapTo(argument->values[index], argument->valuesSize[index], pair->value);
}

void MocaRPCKeyValuePairsWrapToNoSizeSink(int32_t index, MocaRPCKeyValuePair *pair, void *userData)
{
  MocaRPCKeyValuePairsWrapToSinkArgument *argument = static_cast<MocaRPCKeyValuePairsWrapToSinkArgument *>(userData);
  const char *key = argument->keys[index];
  const char *value = argument->values[index];
  MocaRPCStringWrapTo(key, static_cast<int32_t>(strlen(key)), pair->key);
  MocaRPCStringWrapTo(value, static_cast<int32_t>(strlen(value)), pair->value);
}

MocaRPCKeyValuePairs *
MocaRPCKeyValuePairsWrapTo(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize, MocaRPCKeyValuePairs *to)
{
  MocaRPCKeyValuePairsWrapToSinkArgument argument = {keys, values, keysSize, valuesSize};
  if (keysSize && valuesSize) {
    return MocaRPCKeyValuePairsWrapToInternal(size, MocaRPCKeyValuePairsWrapToSink, &argument, to);
  } else {
    return MocaRPCKeyValuePairsWrapToInternal(size, MocaRPCKeyValuePairsWrapToNoSizeSink, &argument, to);
  }
}

MocaRPCKeyValuePairs *
MocaRPCKeyValuePairsWrapTo2(int32_t size, const char **keys, const char **values, MocaRPCKeyValuePairs *to)
{
  return MocaRPCKeyValuePairsWrapTo(size, keys, NULL, values, NULL, to);
}

MocaRPCKeyValuePairs *
MocaRPCKeyValuePairsWrap(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize)
{
  return MocaRPCKeyValuePairsWrapTo(size, keys, keysSize, values, valuesSize, NULL);
}

MocaRPCKeyValuePairs *
MocaRPCKeyValuePairsWrap2(int32_t size, const char **keys, const char **values)
{
  return MocaRPCKeyValuePairsWrapTo(size, keys, NULL, values, NULL, NULL);
}

void MocaRPCKeyValuePairsDestroy(MocaRPCKeyValuePairs *pairs)
{
  free(pairs);
}
