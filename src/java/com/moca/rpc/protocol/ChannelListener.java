package com.moca.rpc.protocol;

import java.util.*;
import java.io.*;

public interface ChannelListener
{
  void onRequest(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize);
  void onResponse(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize);
  void onPayload(Channel channel, long id, int code, InputStream payload, boolean commit);
  void onConnected(Channel channel);
  void onEstablished(Channel channel);
  void onDisconnected(Channel channel);
  void onError(Channel channel, Throwable error);
}
