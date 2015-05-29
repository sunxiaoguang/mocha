package com.moca.rpc.protocol;

import io.netty.buffer.*;
import java.util.*;

public interface ChannelListener
{
  void onRequest(Channel channel, Map<String, String> headers, int payloadSize);
  void onPayload(Channel channel, ByteBuf buffer, boolean commit);
  void onConnect(Channel channel);
  void onDisconnect(Channel channel);
  void onError(Channel channel, Throwable error);
}
