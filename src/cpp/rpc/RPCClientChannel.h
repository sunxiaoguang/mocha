#ifndef __MOCA_RPC_CLIENT_CHANNEL_INTERNAL_H__
#define __MOCA_RPC_CLIENT_CHANNEL_INTERNAL_H__

#include "RPCChannel.h"
#include "RPCProtocol.h"

BEGIN_MOCA_RPC_NAMESPACE

struct WriteRequest;

class RPCClientChannel : public RPCChannelImpl
{
private:
  RPCProtocol protocol_;
  RPCChannelId channelId_;
  RPCServerChannel *owner_;
  ChainedBuffer *buffer_;
  uint64_t lastWriteTime_;
  uint64_t lastReadTime_;

private:
  ChainedBuffer *checkAndGetBuffer(size_t *size);

  virtual int32_t onResolved(struct addrinfo *res);
  virtual void onClose();

  static void onAllocate(uv_handle_t *handle, size_t size, uv_buf_t *buf);
  static void onConnect(uv_connect_t* req, int st);
  static void onRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
  static void onWrite(uv_write_t *req, int32_t st);
  static void onAsyncWrite(RPCOpaqueData req);

  void onConnect();

  void doWrite(ChainedBuffer **buffer);
  int32_t doWriteAsync(WriteRequest *request);
  int32_t doWrite(WriteRequest *request, size_t packetRequestSize);

  static void protocolEventListener(int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);
  static void protocolWriteSink(ChainedBuffer **buffer, RPCOpaqueData argument);

  void updateReadTime();
  void updateWriteTime();

  void updateTime()
  {
    updateReadTime();
    updateWriteTime();
  }

  virtual void onTimer(uint64_t now);

  static void closeChannel(RPCOpaqueData channel) {
    static_cast<RPCClientChannel *>(channel)->close();
  }

public:
  RPCClientChannel();
  virtual ~RPCClientChannel();

  int32_t init(RPCServerChannel *server);
  void onEof();
  void onError();
  void onRead(const char *data, size_t size);

  int32_t keepalive()
  {
    if (isClosing()) {
      return RPC_ILLEGAL_STATE;
    }
    int32_t st = protocol_.keepalive();
    if (st == RPC_CANCELED) {
      updateWriteTime();
      st = RPC_OK;
    }
    return st;
  }
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize)
  {
    if (isClosing()) {
      return RPC_ILLEGAL_STATE;
    }
    return protocol_.response(id, code, headers, payload, payloadSize);
  }
  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize)
  {
    if (isClosing()) {
      return RPC_ILLEGAL_STATE;
    }
    return protocol_.request(id, code, headers, payload, payloadSize);
  }

  RPCChannelId channelId() const { return channelId_; }

  uint64_t lastWriteTime() const { return lastWriteTime_; }
  uint64_t lastReadTime() const { return lastReadTime_; }

  bool isEstablished() const { return protocol_.isEstablished(); }

public:
  RPCClientChannel *readPrev;
  RPCClientChannel *readNext;
  RPCClientChannel *writePrev;
  RPCClientChannel *writeNext;
};

END_MOCA_RPC_NAMESPACE

#endif
