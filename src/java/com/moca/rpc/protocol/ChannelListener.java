package com.moca.rpc.protocol;

import java.util.*;
import java.io.*;

public interface ChannelListener
{
  void onRequest(Channel channel, long id, Map<String, String> headers, int payloadSize);
  void onPayload(Channel channel, long id, InputStream payload, boolean commit);
  void onConnect(Channel channel);
  void onEstablished(Channel channel);
  void onDisconnect(Channel channel);
  void onError(Channel channel, Throwable error);
}
