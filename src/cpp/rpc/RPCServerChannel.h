#ifndef __MOCA_RPC_SERVER_CHANNEL_INTERNAL_H__
#define __MOCA_RPC_SERVER_CHANNEL_INTERNAL_H__

#include "RPCChannel.h"

BEGIN_MOCA_RPC_NAMESPACE

class RPCServerChannel : public RPCChannelImpl
{
private:
  typedef bool (*ChannelVisitor)(RPCClientChannel *channel, RPCOpaqueData userData);
private:
  RPCChannelId currentChannelId_;
  uint32_t activeChannels_;

  RPCClientChannel *readHead_;
  RPCClientChannel *readTail_;
  RPCClientChannel *writeHead_;
  RPCClientChannel *writeTail_;

  uint64_t lastTimeoutTime_;
  uint64_t lastKeepaliveTime_;
private:
  void visitChannel(RPCClientChannel *channel);
  static bool visitChannel(RPCClientChannel *channel, RPCOpaqueData userData);
  virtual void onTimer(uint64_t now);

  static void linkTo(RPCClientChannel **head, RPCClientChannel **tail, RPCClientChannel *item, size_t prev, size_t next);
  static void unlinkFrom(RPCClientChannel **head, RPCClientChannel **tail, RPCClientChannel *item, size_t prev, size_t next);
  static void visitChannel(RPCClientChannel **head, RPCClientChannel **tail, size_t prev, size_t next, int32_t limit, ChannelVisitor visitor, RPCOpaqueData userData);

  static bool sendHeartbeat(RPCClientChannel *channel, RPCOpaqueData userData);
  static bool checkTimeout(RPCClientChannel *channel, RPCOpaqueData userData);

protected:
  virtual void onAsyncClose();

public:
  RPCServerChannel();

  virtual ~RPCServerChannel();

  RPCChannelId addClientChannel(RPCClientChannel *channel);
  void removeClientChannel(RPCChannelId channelId, RPCClientChannel *channel);
  void updateReadTime(RPCChannelId channelId, RPCClientChannel *channel);
  void updateWriteTime(RPCChannelId channelId, RPCClientChannel *channel);

  virtual int32_t onResolved(struct addrinfo *res);
  void onNewConnection();
  static void onNewConnection(uv_stream_t *server, int status);
};

END_MOCA_RPC_NAMESPACE

#endif
