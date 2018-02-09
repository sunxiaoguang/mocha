#include "mocha/rpc-c/RPC.h"
#include "RPCNano.h"
#include <stdio.h>
#include <arpa/inet.h>
#include <pthread.h>

BEGIN_MOCHA_RPC_NAMESPACE
void
StringLite::resize(size_t size)
{
  if (size_ < size) {
    if (capacity_ < size + 1) {
      capacity_ += MOCHA_RPC_ALIGN(size + 1, 8);
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
    capacity_ += MOCHA_RPC_ALIGN(size_ + len + 1, 8);
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
    capacity_ = MOCHA_RPC_ALIGN(len + 1, 8);
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

RPCThreadLocalKey::RPCThreadLocalKey(RPCOpaqueDataDestructor destructor)
{
  int32_t rc = pthread_key_create(&key_, destructor);

  rc++;
}

RPCThreadLocalKey::~RPCThreadLocalKey()
{
  pthread_key_delete(key_);
}

void RPCThreadLocalKey::set(RPCOpaqueData data)
{
  pthread_setspecific(key_, data);
}

RPCOpaqueData RPCThreadLocalKey::doGet()
{
  return pthread_getspecific(key_);
}

void RPCOnce::run(void (*routine)(void))
{
  pthread_once(&once_, routine);
}

RPCThread::RPCThread(RPCThread::EntryPoint entry, RPCOpaqueData argument)
  : entry_(entry), argument_(argument), running_(false)
{
  memset(&thread_, 0, sizeof(thread_));
}

RPCOpaqueData
RPCThread::run(RPCOpaqueData context)
{
  RPCThread *thread = static_cast<RPCThread *>(context);
  thread->entry_(thread->argument_);
  return NULL;
}

int32_t RPCThread::start()
{
  if (running_) {
    return RPC_ILLEGAL_STATE;
  }
  running_ = true;
  int32_t rc = pthread_create(&thread_, NULL, run, this);
  return rc == 0 ? RPC_OK : RPC_INSUFFICIENT_RESOURCE;
}

int32_t RPCThread::join(RPCOpaqueData *result)
{
  if (!running_) {
    return RPC_ILLEGAL_STATE;
  }
  RPCOpaqueData res;
  if (result == NULL) {
    result = &res;
  }
  int32_t rc = pthread_join(thread_, result);
  return rc == 0 ? RPC_OK : RPC_ILLEGAL_STATE;
}

RPCThread::~RPCThread()
{
}

bool RPCThread::operator==(const RPCThread &rhs) const
{
  return pthread_equal(thread_, rhs.thread_);
}

END_MOCHA_RPC_NAMESPACE

const char *MochaRPCErrorString(int32_t code)
{
  switch (code) {
    case MOCHA_RPC_OK:
      return "Ok";
    case MOCHA_RPC_INVALID_ARGUMENT:
      return "Invalid argument";
    case MOCHA_RPC_CORRUPTED_DATA:
      return "Corrupted data";
    case MOCHA_RPC_OUT_OF_MEMORY:
      return "Running out of memory";
    case MOCHA_RPC_CAN_NOT_CONNECT:
      return "Can not connect to remote server";
    case MOCHA_RPC_NO_ACCESS:
      return "No access";
    case MOCHA_RPC_NOT_SUPPORTED:
      return "Not supported";
    case MOCHA_RPC_TOO_MANY_OPEN_FILE:
      return "Too many open files";
    case MOCHA_RPC_INSUFFICIENT_RESOURCE:
      return "Insufficient resource";
    case MOCHA_RPC_INTERNAL_ERROR:
      return "Internal error";
    case MOCHA_RPC_ILLEGAL_STATE:
      return "Illegal state";
    case MOCHA_RPC_TIMEOUT:
      return "Socket timeout";
    case MOCHA_RPC_DISCONNECTED:
      return "Disconnected";
    case MOCHA_RPC_WOULDBLOCK:
      return "Operation would block";
    case MOCHA_RPC_INCOMPATIBLE_PROTOCOL:
      return "Incompatible protocol";
    case MOCHA_RPC_CAN_NOT_BIND:
      return "Can not bind socket to given name";
    case MOCHA_RPC_BUFFER_OVERFLOW:
      return "Running out of buffer";
    default:
      return "Unknown Error";
  }
}

using namespace mocha::rpc;

#define MOCHA_RPC_STRING_SIZE(len) (len + OFFSET_OF(MochaRPCString, content) + 1)
#define MOCHA_RPC_KEY_VALUE_PAIR_SIZE(klen, vlen) (MOCHA_RPC_STRING_SIZE(klen) + MOCHA_RPC_STRING_SIZE(vlen) + OFFSET_OF(MochaRPCKeyValuePair, extra))

MochaRPCString *
convert(const char *str, int32_t len, MochaRPCString *output)
{
  memcpy(output->content.buffer, str, len);
  output->content.buffer[len] = '\0';
  output->flags = MOCHA_RPC_STRING_FLAG_EMBEDDED;
  MOCHA_SAFE_POINTER_WRITE(len, &output->size, int32_t);
  return output;
}

MochaRPCString *
MochaRPCStringCreate(const char *str, int32_t len)
{
  size_t realSize = MOCHA_RPC_STRING_SIZE(len);
  return convert(str, len, static_cast<MochaRPCString *>(malloc(realSize)));
}

MochaRPCString *
MochaRPCStringCreate2(const char *str)
{
  /* we don't want to support huge string longer than 2GB */
  return MochaRPCStringCreate(str, static_cast<int32_t>(strlen(str)));
}

MochaRPCString *
MochaRPCStringWrapTo(const char *str, int32_t length, MochaRPCString *to)
{
  to->size = length;
  to->flags = 0;
  to->content.ptr = str;
  return to;
}

MochaRPCString *
MochaRPCStringWrapTo2(const char *str, MochaRPCString *to)
{
  return MochaRPCStringWrapTo(str, static_cast<int32_t>(strlen(str)), to);
}

MochaRPCString *
MochaRPCStringWrap(const char *str, int32_t length)
{
  return MochaRPCStringWrapTo(str, length, static_cast<MochaRPCString *>(malloc(sizeof(MochaRPCString))));
}

MochaRPCString *
MochaRPCStringWrap2(const char *str)
{
  /* we don't want to support huge string longer than 2GB */
  return MochaRPCStringWrap(str, static_cast<int32_t>(strlen(str)));
}

void
MochaRPCStringDestroy(MochaRPCString *string)
{
  free(string);
}

const char *
MochaRPCStringGet(const MochaRPCString *str)
{
  if (str->flags & MOCHA_RPC_STRING_FLAG_EMBEDDED) {
    return str->content.buffer;
  } else {
    return str->content.ptr;
  }
}

MochaRPCKeyValuePair *
convert(const char *key, int32_t keyLen, const char *value, int32_t valueLen, MochaRPCKeyValuePair *pair)
{
  size_t keySize = MOCHA_RPC_STRING_SIZE(keyLen);
  pair->key = unsafeGet<MochaRPCString>(pair->extra, 0);
  pair->value = unsafeGet<MochaRPCString>(pair->extra, keySize);
  convert(key, keyLen, pair->key);
  convert(value, valueLen, pair->value);
  return pair;
}

MochaRPCKeyValuePair *
MochaRPCKeyValuePairCreate(const char *key, int32_t keyLen, const char *value, int32_t valueLen)
{
  size_t realSize = MOCHA_RPC_KEY_VALUE_PAIR_SIZE(keyLen, valueLen);
  return convert(key, keyLen, value, valueLen, static_cast<MochaRPCKeyValuePair *>(malloc(realSize)));
}

MochaRPCKeyValuePair *
MochaRPCKeyValuePairCreate2(const char *key, const char *value)
{
  return MochaRPCKeyValuePairCreate(key, static_cast<int32_t>(strlen(key)), value, static_cast<int32_t>(strlen(value)));
}

MochaRPCKeyValuePair *
MochaRPCKeyValuePairWrapTo(const char *key, int32_t keyLen, const char *value, int32_t valueLen, MochaRPCKeyValuePair *to)
{
  MochaRPCStringWrapTo(key, keyLen, to->key);
  MochaRPCStringWrapTo(value, valueLen, to->value);
  return to;
}

MochaRPCKeyValuePair *
MochaRPCKeyValuePairWrap(const char *key, int32_t keyLen, const char *value, int32_t valueLen)
{
  MochaRPCKeyValuePair *pair = static_cast<MochaRPCKeyValuePair *>(malloc(MOCHA_RPC_KEY_VALUE_PAIR_SIZE(sizeof(void *), sizeof(void *))));
  pair->key = unsafeGet<MochaRPCString>(pair->extra, 0);
  pair->value = unsafeGet<MochaRPCString>(pair->extra, MOCHA_RPC_STRING_SIZE(sizeof(void*)));
  return MochaRPCKeyValuePairWrapTo(key, keyLen, value, valueLen, pair);
}

MochaRPCKeyValuePair *
MochaRPCKeyValuePairWrap2(const char *key, const char *value)
{
  return MochaRPCKeyValuePairWrap(key, static_cast<int32_t>(strlen(key)), value, static_cast<int32_t>(strlen(value)));
}

void
MochaRPCKeyValuePairDestroy(MochaRPCKeyValuePair *pair)
{
  free(pair);
}

MochaRPCKeyValuePairs *
convert(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize, MochaRPCKeyValuePairs *pairs)
{
  MOCHA_SAFE_POINTER_WRITE(size, &pairs->size, int32_t);
  pairs->pair = unsafeGet<MochaRPCKeyValuePair *>(pairs->extra, 0);
  size_t offset = size * sizeof(MochaRPCKeyValuePair *);
  for (int32_t idx = 0; idx < size; ++idx) {
    int32_t keyLen = keysSize[idx];
    int32_t valueLen = valuesSize[idx];
    int32_t pairSize = MOCHA_RPC_KEY_VALUE_PAIR_SIZE(keyLen, valueLen);
    MochaRPCKeyValuePair *pair = unsafeGet<MochaRPCKeyValuePair>(pairs->extra, offset);
    convert(keys[idx], keyLen, values[idx], valueLen, pair);
    offset += pairSize;
    MOCHA_SAFE_POINTER_WRITE(pair, pairs->pair + idx, MochaRPCKeyValuePair *);
  }

  return pairs;
}

MochaRPCKeyValuePairs *
MochaRPCKeyValuePairsCreate(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize)
{
  size_t totalSize = OFFSET_OF(MochaRPCKeyValuePairs, extra) + (size * sizeof(MochaRPCKeyValuePair *));

  for (int32_t idx = 0; idx < size; ++idx) {
    totalSize += MOCHA_RPC_KEY_VALUE_PAIR_SIZE(keysSize[idx], valuesSize[idx]);
  }

  return convert(size, keys, keysSize, values, valuesSize, static_cast<MochaRPCKeyValuePairs *>(malloc(totalSize)));
}

MochaRPCKeyValuePairs *
MochaRPCKeyValuePairsCreate2(int32_t size, const char **keys, const char **values)
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
  size_t totalSize = OFFSET_OF(MochaRPCKeyValuePairs, extra) + (size * sizeof(MochaRPCKeyValuePair *));

  for (int32_t idx = 0; idx < size; ++idx) {
    int32_t keySize = static_cast<int32_t>(strlen(keys[idx]));
    keysSize[idx] = keySize;
    int32_t valueSize = static_cast<int32_t>(strlen(values[idx]));
    valuesSize[idx] = valueSize;
    totalSize += MOCHA_RPC_KEY_VALUE_PAIR_SIZE(keySize, valueSize);
  }

  return convert(size, keys, keysSize, values, valuesSize, static_cast<MochaRPCKeyValuePairs *>(malloc(totalSize)));
}

MochaRPCKeyValuePairs *
MochaRPCKeyValuePairsWrapToInternal(int32_t size, MochaRPCKeyValuePairsWrapToInternalSink sink, void *sinkUserData, MochaRPCKeyValuePairs *to)
{
  if (to == NULL || to->capacity < size || to->capacity - size > 32) {
    MochaRPCKeyValuePairsDestroy(to);
    int32_t capacity = MOCHA_RPC_ALIGN(size, 8);
    size_t headerBufferOffset = OFFSET_OF(MochaRPCKeyValuePairs, extra) + (capacity * sizeof(MochaRPCKeyValuePair *));
    size_t stringBufferOffset = headerBufferOffset + (capacity * OFFSET_OF(MochaRPCKeyValuePair, extra));
    size_t totalSize = stringBufferOffset + (capacity * 2 * sizeof(MochaRPCString));

    MochaRPCKeyValuePairs *pairs = static_cast<MochaRPCKeyValuePairs *>(malloc(totalSize));
    pairs->pair = unsafeGet<MochaRPCKeyValuePair*>(pairs->extra, 0);
    pairs->capacity = capacity;
    MochaRPCString *string = unsafeGet<MochaRPCString>(pairs, stringBufferOffset);

    for (int32_t idx = 0; idx < capacity; ++idx) {
      MochaRPCKeyValuePair *pair = unsafeGet<MochaRPCKeyValuePair>(pairs, headerBufferOffset + (idx * OFFSET_OF(MochaRPCKeyValuePair, extra)));
      pairs->pair[idx] = pair;
      pair->key = string++;
      pair->value = string++;
    }
    to = pairs;
  }

  MochaRPCKeyValuePair **headers = to->pair;
  for (int32_t idx = 0; idx < size; ++idx) {
    sink(idx, headers[idx], sinkUserData);
  }
  to->size = size;

  return to;
}

struct MochaRPCKeyValuePairsWrapToSinkArgument {
  const char **keys;
  const char **values;
  const int32_t *keysSize;
  const int32_t *valuesSize;
};

void MochaRPCKeyValuePairsWrapToSink(int32_t index, MochaRPCKeyValuePair *pair, void *userData)
{
  MochaRPCKeyValuePairsWrapToSinkArgument *argument = static_cast<MochaRPCKeyValuePairsWrapToSinkArgument *>(userData);
  MochaRPCStringWrapTo(argument->keys[index], argument->keysSize[index], pair->key);
  MochaRPCStringWrapTo(argument->values[index], argument->valuesSize[index], pair->value);
}

void MochaRPCKeyValuePairsWrapToNoSizeSink(int32_t index, MochaRPCKeyValuePair *pair, void *userData)
{
  MochaRPCKeyValuePairsWrapToSinkArgument *argument = static_cast<MochaRPCKeyValuePairsWrapToSinkArgument *>(userData);
  const char *key = argument->keys[index];
  const char *value = argument->values[index];
  MochaRPCStringWrapTo(key, static_cast<int32_t>(strlen(key)), pair->key);
  MochaRPCStringWrapTo(value, static_cast<int32_t>(strlen(value)), pair->value);
}

MochaRPCKeyValuePairs *
MochaRPCKeyValuePairsWrapTo(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize, MochaRPCKeyValuePairs *to)
{
  MochaRPCKeyValuePairsWrapToSinkArgument argument = {keys, values, keysSize, valuesSize};
  if (keysSize && valuesSize) {
    return MochaRPCKeyValuePairsWrapToInternal(size, MochaRPCKeyValuePairsWrapToSink, &argument, to);
  } else {
    return MochaRPCKeyValuePairsWrapToInternal(size, MochaRPCKeyValuePairsWrapToNoSizeSink, &argument, to);
  }
}

MochaRPCKeyValuePairs *
MochaRPCKeyValuePairsWrapTo2(int32_t size, const char **keys, const char **values, MochaRPCKeyValuePairs *to)
{
  return MochaRPCKeyValuePairsWrapTo(size, keys, NULL, values, NULL, to);
}

MochaRPCKeyValuePairs *
MochaRPCKeyValuePairsWrap(int32_t size, const char **keys, const int32_t *keysSize, const char **values, const int32_t *valuesSize)
{
  return MochaRPCKeyValuePairsWrapTo(size, keys, keysSize, values, valuesSize, NULL);
}

MochaRPCKeyValuePairs *
MochaRPCKeyValuePairsWrap2(int32_t size, const char **keys, const char **values)
{
  return MochaRPCKeyValuePairsWrapTo(size, keys, NULL, values, NULL, NULL);
}

void MochaRPCKeyValuePairsDestroy(MochaRPCKeyValuePairs *pairs)
{
  free(pairs);
}
