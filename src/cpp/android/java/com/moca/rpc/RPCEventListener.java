package com.moca.rpc;

import java.util.*;
import java.io.*;

public interface RPCEventListener
{
  void onRequest(RPC channel, long id, int code, KeyValuePair[] headers, int payloadSize);
  void onResponse(RPC channel, long id, int code, KeyValuePair[] headers, int payloadSize);
  void onPayload(RPC channel, long id, InputStream payload, boolean commit);
  void onConnected(RPC channel);
  void onEstablished(RPC channel);
  void onDisconnected(RPC channel);
  void onError(RPC channel, Throwable error);
}
