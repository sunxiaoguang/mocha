package com.moca.rpc;

import java.net.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

public class RPC
{
  public static int LOOP_FLAG_ONE_SHOT = 1;
  public static int LOOP_FLAG_ONE_NONBLOCK = 2;
  private volatile long handle = Long.MIN_VALUE;
  private volatile long globalRef = Long.MIN_VALUE;
  private AtomicLongFieldUpdater handleUpdater = AtomicLongFieldUpdater.newUpdater(RPC.class, "handle");
  private AtomicInteger refcount = new AtomicInteger(1);
  private RPCEventListener listener;
  private ThreadLocal<Boolean> onError = new ThreadLocal<Boolean>() {
    @Override
    public Boolean initialValue() {
      return Boolean.FALSE;
    }
  };

  static {
    System.loadLibrary("mocarpc_jni");
  }

  private native static void doCreate(String address, long timeout, long keepalive, RPC channel);
  private native static void doDestroy(RPC channel, long handle);

  private native static String doGetLocalAddress(long handle);
  private native static int doGetLocalPort(long handle);
  private native static String doGetRemoteAddress(long handle);
  private native static int doGetRemotePort(long handle);

  private native static String doGetLocalId(long handle);
  private native static String doGetRemoteId(long handle);

  private native static void doLoop(long handle, int flags);
  private native static void doBreakLoop(long handle);
  private native static void doKeepAlive(long handle);

  private native static void doResponse(long handle, long id, int code, KeyValuePair[] headers, byte[] payload, int offset, int size);
  private native static void doRequest(long handle, int code, KeyValuePair[] headers, byte[] payload, int offset, int size);

  private static String toString(byte[] utf8)
  {
    try {
      return new String(utf8, "UTF-8");
    } catch (UnsupportedEncodingException ex) {
      throw new RuntimeException(ex);
    }
  }

  private void dispatchRequestEvent(long id, int code, KeyValuePair[] headers, int payloadSize)
  {
    listener.onRequest(this, id, code, headers, payloadSize);
  }
  private void dispatchResponseEvent(long id, int code, KeyValuePair[] headers, int payloadSize)
  {
    listener.onResponse(this, id, code, headers, payloadSize);
  }
  private void dispatchPayloadEvent(long id, byte[] payload, boolean commit)
  {
    listener.onPayload(this, id, new ByteArrayInputStream(payload), commit);
  }
  private void dispatchConnectedEvent()
  {
    listener.onConnected(this);
  }
  private void dispatchEstablishedEvent()
  {
    listener.onEstablished(this);
  }
  private void dispatchDisconnectedEvent()
  {
    listener.onDisconnected(this);
  }
  private void dispatchErrorEvent(int code, String message)
  {
    if (onError.get()) {
      return;
    }
    try {
      onError.set(Boolean.TRUE);
      listener.onError(this, new RuntimeException(message + " : " + code));
    } finally {
      onError.set(Boolean.FALSE);
    }
  }

  private void destroy()
  {
    while (true) {
      long tmp = handle;
      if (handleUpdater.compareAndSet(this, tmp, Long.MIN_VALUE)) {
        try {
          if (tmp != Long.MIN_VALUE) {
            doDestroy(this, tmp);
          }
        } finally {
          break;
        }
      }
    }
  }

  private void addRef()
  {
    if (refcount.getAndIncrement() == 0) {
      throw new RuntimeException("Calling addRef on RPC Channel that has no valid refcount");
    }
  }

  private void release()
  {
    if (refcount.decrementAndGet() == 0) {
      destroy();
    }
  }

  private RPC(String address, long timeout, long keepalive, RPCEventListener listener)
  {
    this.listener = listener;
    doCreate(address, timeout, keepalive, this);
  }

  public static class Builder
  {
    private String address;
    private long timeout = 12000000;
    private long keepalive = 6000000;
    private RPCEventListener listener;

    public Builder connect(String address) throws UnknownHostException
    {
      String fullAddress = address;
      int pos = address.indexOf(":");
      if (pos >= 0) {
        address = address.substring(0, pos);
      } else {
        throw new IllegalArgumentException("Invalid server address '" + address +  "', port number is not specified");
      }
      InetAddress tmp = InetAddress.getByName(address);
      this.address = fullAddress;
      return this;
    }
    public Builder timeout(long timeout, TimeUnit unit)
    {
      this.timeout = unit.toMicros(timeout);
      return this;
    }
    public Builder listener(RPCEventListener listener)
    {
      this.listener = listener;
      return this;
    }
    public Builder keepalive(long interval, TimeUnit unit)
    {
      this.keepalive = unit.toMicros(interval);
      return this;
    }

    public RPC build()
    {
      if (address == null) {
        throw new IllegalStateException("Server address is not set");
      }
      if (listener == null) {
        throw new IllegalStateException("Event listener is not set");
      }
      RPC rpc = null;
      RPC result;
      try {
        rpc = new RPC(address, timeout, keepalive, listener);
        result = rpc;
        rpc = null;
        return result;
      } finally {
        if (rpc != null) {
          try {
            rpc.close();
          } catch (Exception ex) {
            throw new RuntimeException(ex);
          }
        }
      }
    }
  }

  public static Builder builder()
  {
    return new Builder();
  }

  public void close()
  {
    release();
  }

  public InetSocketAddress getLocalAddress()
  {
    try {
      addRef();
      return new InetSocketAddress(doGetLocalAddress(handle), doGetLocalPort(handle));
    } finally {
      release();
    }
  }
  public InetSocketAddress getRemoteAddress()
  {
    try {
      addRef();
      return new InetSocketAddress(doGetRemoteAddress(handle), doGetRemotePort(handle));
    } finally {
      release();
    }
  }

  public String getLocalId()
  {
    try {
      addRef();
      return doGetLocalId(handle);
    } finally {
      release();
    }
  }

  public String getRemoteId()
  {
    try {
      addRef();
      return doGetRemoteId(handle);
    } finally {
      release();
    }
  }

  public void loop()
  {
    loop(0);
  }

  public void loop(int flags)
  {
    try {
      addRef();
      doLoop(handle, flags);
    } finally {
      release();
    }
  }

  public void breakLoop()
  {
    try {
      addRef();
      doBreakLoop(handle);
    } finally {
      release();
    }
  }

  public void keepAlive()
  {
    try {
      addRef();
      doKeepAlive(handle);
    } finally {
      release();
    }
  }

  public void response(long id, int code, KeyValuePair[] headers, byte[] payload, int offset, int size)
  {
    try {
      addRef();
      doResponse(handle, id, code, headers, payload, offset, size);
    } finally {
      release();
    }
  }

  public void request(int code, KeyValuePair[] headers, byte[] payload, int offset, int size)
  {
    try {
      addRef();
      doRequest(handle, code, headers, payload, offset, size);
    } finally {
      release();
    }
  }

  public void response(long id, int code, KeyValuePair[] headers, byte[] payload)
  {
    response(id, code, headers, payload, 0, payload.length);
  }
  public void response(long id, int code, KeyValuePair[] headers)
  {
    response(id, code, headers, null, 0, 0);
  }

  public void response(long id, int code, Map<String, String> headers, byte[] payload, int offset, int size)
  {
    response(id, code, convert(headers), payload, 0, payload.length);
  }

  public void response(long id, int code, Map<String, String> headers, byte[] payload)
  {
    response(id, code, headers, payload, 0, payload.length);
  }
  public void response(long id, int code, Map<String, String> headers)
  {
    response(id, code, headers, null, 0, 0);
  }
  public void response(long id, int code)
  {
    response(id, code, (KeyValuePair[]) null, null, 0, 0);
  }

  private KeyValuePair[] convert(Map<String, String> headers)
  {
    KeyValuePair[] pairs = new KeyValuePair[headers.size()];
    int idx = 0;
    for (Map.Entry<String, String> entry : headers.entrySet()) {
      pairs[idx++] = new KeyValuePair(entry.getKey(), entry.getValue());
    }
    return pairs;
  }

  public void request(int code, KeyValuePair[] headers, byte[] payload)
  {
    request(code, headers, payload, 0, payload.length);
  }
  public void request(int code, KeyValuePair[] headers)
  {
    request(code, headers, null, 0, 0);
  }

  public void request(int code, Map<String, String> headers, byte[] payload, int offset, int size)
  {
    request(code, convert(headers), payload, offset, size);
  }

  public void request(int code, Map<String, String> headers, byte[] payload)
  {
    request(code, headers, payload, 0, payload.length);
  }
  public void request(int code, Map<String, String> headers)
  {
    request(code, headers, null, 0, 0);
  }
  public void request(int code)
  {
    request(code, (KeyValuePair []) null, null, 0, 0);
  }
}
