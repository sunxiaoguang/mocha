package com.mocha.rpc.protocol;

import java.util.*;
import java.util.concurrent.*;
import java.net.*;

import com.mocha.rpc.protocol.impl.*;

public interface Channel
{
  Future response(long id, int code, KeyValuePair[] headers, byte[] payload, int offset, int size);
  Future response(long id, int code, KeyValuePair[] headers, byte[] payload);
  Future response(long id, int code, KeyValuePair[] headers);
  Future response(long id, int code, Map<String, String> headers, byte[] payload, int offset, int size);
  Future response(long id, int code, Map<String, String> headers, byte[] payload);
  Future response(long id, int code, Map<String, String> headers);
  Future response(long id, int code);

  Future request(int code, KeyValuePair[] headers, byte[] payload, int offset, int size);
  Future request(int code, KeyValuePair[] headers, byte[] payload);
  Future request(int code, KeyValuePair[] headers);
  Future request(int code, Map<String, String> headers, byte[] payload, int offset, int size);
  Future request(int code, Map<String, String> headers, byte[] payload);
  Future request(int code, Map<String, String> headers);
  Future request(int code);

  Future shutdown();

  InetSocketAddress getLocalAddress();
  InetSocketAddress getRemoteAddress();

  String getLocalId();
  String getRemoteId();

  <T> void setAttachment(T attachment);
  Object getAttachment();
  <T> T getAttachment(Class<T> type);
}
