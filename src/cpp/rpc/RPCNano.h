#ifndef __MOCA_RPC_NANO_INTERNAL_H__
#define __MOCA_RPC_NANO_INTERNAL_H__ 1
#include "moca/rpc/RPC.h"
#include "RPCLogging.h"
#include <time.h>
#include <sys/socket.h>
#include <stdio.h>

#ifdef MOCA_RPC_NANO
#include <pthread.h>
#endif

#ifndef ANDROID
  #include <uuid/uuid.h>
#endif

#define INT32_SWAP(value) \
  ( \
   ((value & 0x000000ffUL) << 24) | \
   ((value & 0x0000ff00UL) << 8) | \
   ((value & 0x00ff0000UL) >> 8) | \
   ((value & 0xff000000UL) >> 24) \
  )

#define SECOND (1000000)

#define OFFSET_OF(TYPE, FIELD) (((size_t)(&((TYPE *) sizeof(TYPE))->FIELD)) - sizeof(TYPE))
#define CHAINED_BUFFER_SIZE(size) MOCA_RPC_ALIGN(size + OFFSET_OF(ChainedBuffer, buffer), 8)

#include "RPCAtomic.h"

BEGIN_MOCA_RPC_NAMESPACE
static const size_t MAX_INT32 = 0x7FFFFFFF;
/* magic code is MOCA in ascii */
static const uint32_t MAGIC_CODE = 0X4D4F4341;

class FriendHelper
{
private:
  void *impl_;
public:
  template<typename T>
  static T *getImpl(const void *wrapper) {
    FriendHelper *helper = static_cast<FriendHelper *>(const_cast<void *>(wrapper));
    return static_cast<T *>(helper->impl_);
  }
};

template<typename T>
inline T *unsafeGet(void *base, size_t offset)
{
  return reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

struct ChainedBuffer
{
  ChainedBuffer *next;
  int32_t size;
  int32_t capacity;
  uint8_t buffer[sizeof(int64_t)];

  int32_t available()
  {
    return capacity - size;
  }

  size_t getSize() const
  {
    return size;
  }

  uint8_t *get(size_t offset = 0)
  {
    return buffer + offset;
  }

  template <typename T>
  T *get(size_t offset = 0)
  {
    return unsafeGet<T>(buffer, offset);
  }

  template <typename T>
  T *allocate()
  {
    return allocate<T>(sizeof(T));
  }

  template <typename T>
  T *allocate(size_t sz)
  {
    return unsafeGet<T>(allocate(sz), 0);
  }

  uint8_t *allocate(size_t sz)
  {
    uint8_t *result = buffer + size;
    size += sz;
    return result;
  }

  ChainedBuffer(int32_t c) : next(NULL), size(0), capacity(c)
  {
  }

  static void destroy(ChainedBuffer **bufferAddress, size_t limit = UINTPTR_MAX)
  {
    ChainedBuffer *next;
    ChainedBuffer *buffer = *bufferAddress;
    while (buffer && limit > 0) {
      next = buffer->next;
      free(buffer);
      buffer = next;
      --limit;
    }
    *bufferAddress = buffer;
  }

  static ChainedBuffer *create(int32_t size, ChainedBuffer ***nextAddress)
  {
    int32_t allocationSize = CHAINED_BUFFER_SIZE(size);
    int32_t capacity = allocationSize - OFFSET_OF(ChainedBuffer, buffer);
    void *ptr = malloc(allocationSize);
    ChainedBuffer *newBuffer = new (ptr) ChainedBuffer(capacity);
    **nextAddress = newBuffer;
    *nextAddress = &newBuffer->next;
    return newBuffer;
  }

  static ChainedBuffer *checkBuffer(ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextAddress, int32_t *numBuffers, size_t required)
  {
    if (!*current || (*current)->available() < static_cast<int32_t>(required)) {
      *current = ChainedBuffer::create(required, nextAddress);
      if (!*head) {
        *head = *current;
      }
      (*numBuffers)++;
    }
    return *current;
  }

};

struct NegotiationHeader
{
  uint32_t magicCode;
  int32_t flags;
  int32_t idSize;
  char id[4];
};

struct PacketHeader
{
  int64_t id;
  int32_t code;
  int32_t flags;
  int32_t headerSize;
  int32_t payloadSize;
};

time_t getDeadline(int64_t timeout);
bool isTimeout(time_t deadline);

class RPCObject
{
private:
  RPCAtomic<int32_t> refcount_;

public:
  RPCObject() : refcount_(1) { }
  virtual ~RPCObject();

  void addRef() const { refcount_.add(1); }
  bool release() const { bool result = refcount_.subtract(1) == 1; if (result) { delete this; } return result; }
};

void uuidGenerate(StringLite *output);

int32_t getInetAddressPresentation(const sockaddr_storage *addr, StringLite *localAddress, uint16_t *port);

END_MOCA_RPC_NAMESPACE

typedef void (*MocaRPCKeyValuePairsWrapToInternalSink)(int32_t index, MocaRPCKeyValuePair *pair, void *userData);

MocaRPCKeyValuePairs *
MocaRPCKeyValuePairsWrapToInternal(int32_t size, MocaRPCKeyValuePairsWrapToInternalSink sink, void *sinkUserData, MocaRPCKeyValuePairs *to);

#endif
