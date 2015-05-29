package com.moca.rpc.protocol;

import java.util.*;
import java.io.*;

public interface ChannelListener
{
  void onRequest(Channel channel, Map<String, String> headers, int payloadSize);
  void onPayload(Channel channel, InputStream payload, boolean commit);
  void onConnect(Channel channel);
  void onDisconnect(Channel channel);
  void onError(Channel channel, Throwable error);
}
