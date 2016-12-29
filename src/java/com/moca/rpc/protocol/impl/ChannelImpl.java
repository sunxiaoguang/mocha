package com.moca.rpc.protocol.impl;

import com.moca.rpc.protocol.*;

import java.net.*;
import java.util.*;
import java.util.zip.*;
import java.util.concurrent.atomic.*;
import java.util.concurrent.*;
import java.nio.*;
import java.io.*;

public abstract class ChannelImpl implements com.moca.rpc.protocol.Channel
{
  public final static int MAX_CHANNEL_ID_LENGTH = 128;

  private InetSocketAddress localAddress;
  private InetSocketAddress remoteAddress;
  private volatile Object attachment;

  private String localId;
  private volatile String remoteId = "NOT_AVAILABLE";

  protected ChannelImpl(String id)
  {
    this.localId = id;
  }

  @Override
  public String getLocalId()
  {
    return localId;
  }

  @Override
  public String getRemoteId()
  {
    return remoteId;
  }

  protected void init(InetSocketAddress localAddress, InetSocketAddress remoteAddress)
  {
    this.localAddress = localAddress;
    this.remoteAddress = remoteAddress;
  }

  public InetSocketAddress getLocalAddress()
  {
    return localAddress;
  }

  public InetSocketAddress getRemoteAddress()
  {
    return remoteAddress;
  }

  protected void remoteId(String id)
  {
    this.remoteId = id;
    synchronized (this) {
      notifyAll();
    }
  }

  public abstract Future response(long id, int code, KeyValuePair[] headers, byte[] payload, int offset, int size);

  public Future response(long id, int code, KeyValuePair[] headers, byte[] payload)
  {
    return response(id, code, headers, payload, 0, payload.length);
  }
  public Future response(long id, int code, KeyValuePair[] headers)
  {
    return response(id, code, headers, null, 0, 0);
  }

  public Future response(long id, int code, Map<String, String> headers, byte[] payload, int offset, int size)
  {
    return response(id, code, convert(headers), payload, 0, payload.length);
  }

  public Future response(long id, int code, Map<String, String> headers, byte[] payload)
  {
    return response(id, code, headers, payload, 0, payload.length);
  }
  public Future response(long id, int code, Map<String, String> headers)
  {
    return response(id, code, headers, null, 0, 0);
  }
  public Future response(long id, int code)
  {
    return response(id, code, (KeyValuePair[]) null, null, 0, 0);
  }

  public abstract Future request(int code, KeyValuePair[] headers, byte[] payload, int offset, int size);

  private KeyValuePair[] convert(Map<String, String> headers)
  {
    KeyValuePair[] pairs = new KeyValuePair[headers.size()];
    int idx = 0;
    for (Map.Entry<String, String> entry : headers.entrySet()) {
      pairs[idx++] = new KeyValuePair(entry.getKey(), entry.getValue());
    }
    return pairs;
  }

  public Future request(int code, KeyValuePair[] headers, byte[] payload)
  {
    return request(code, headers, payload, 0, payload.length);
  }
  public Future request(int code, KeyValuePair[] headers)
  {
    return request(code, headers, null, 0, 0);
  }

  public Future request(int code, Map<String, String> headers, byte[] payload, int offset, int size)
  {
    return request(code, convert(headers), payload, offset, size);
  }

  public Future request(int code, Map<String, String> headers, byte[] payload)
  {
    return request(code, headers, payload, 0, payload.length);
  }
  public Future request(int code, Map<String, String> headers)
  {
    return request(code, headers, null, 0, 0);
  }
  public Future request(int code)
  {
    return request(code, (KeyValuePair []) null, null, 0, 0);
  }
  public abstract Future shutdown();

  public String toString()
  {
    return getRemoteAddress() + " => " + getLocalAddress();
  }

  public <T> void setAttachment(T attachment)
  {
    this.attachment = attachment;
  }

  public Object getAttachment()
  {
    return attachment;
  }

  public <T> T getAttachment(Class<T> type)
  {
    return (T) getAttachment();
  }

  public static byte[] toUtf8(String str)
  {
    try {
      return str.getBytes("UTF-8");
    } catch (UnsupportedEncodingException ex) {
      throw new RuntimeException(ex);
    }
  }

  public static String fromUtf8(byte[] utf8)
  {
    try {
      return new String(utf8, "UTF-8");
    } catch (UnsupportedEncodingException ex) {
      throw new RuntimeException(ex);
    }
  }
}
