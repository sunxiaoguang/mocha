package com.mocha.rpc.protocol.impl;

import com.mocha.rpc.protocol.*;

import java.net.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

public abstract class JniChannel extends ChannelImpl
{
  protected volatile long handle = Long.MIN_VALUE;
  private volatile long globalRef = Long.MIN_VALUE;
  private AtomicLongFieldUpdater handleUpdater = AtomicLongFieldUpdater.newUpdater(JniChannel.class, "handle");
  private AtomicInteger refcount = new AtomicInteger(1);
  private boolean isNano;
  private ThreadLocal<Boolean> onError = new ThreadLocal<Boolean>() {
    @Override
    public Boolean initialValue() {
      return Boolean.FALSE;
    }
  };

  static {
    System.loadLibrary("mocharpc_nano_jni");
  }

  private native static void doCreate(boolean nano, String address, long timeout, long keepalive, Channel channel);
  private native static void doDestroy(boolean nano, JniChannel channel, long handle);

  private native static String doGetLocalAddress(boolean nano, long handle);
  private native static int doGetLocalPort(boolean nano, long handle);
  private native static String doGetRemoteAddress(boolean nano, long handle);
  private native static int doGetRemotePort(boolean nano, long handle);

  private native static String doGetLocalId(boolean nano, long handle);
  private native static String doGetRemoteId(boolean nano, long handle);

  private native static void doResponse(boolean nano, long handle, long id, int code, KeyValuePair[] headers, byte[] payload, int offset, int size);
  private native static long doRequest(boolean nano, long handle, int code, KeyValuePair[] headers, byte[] payload, int offset, int size);

  protected void jniInit(InetSocketAddress address, long timeout, long keepalive)
  {
    doCreate(isNano, address.getAddress().getHostAddress() + ":" + address.getPort(), timeout, keepalive, this);
  }

  protected InetSocketAddress localAddress()
  {
    try {
      addRef();
      return new InetSocketAddress(doGetLocalAddress(isNano, handle), doGetLocalPort(isNano, handle));
    } finally {
      release();
    }
  }
  public InetSocketAddress remoteAddress()
  {
    try {
      addRef();
      return new InetSocketAddress(doGetRemoteAddress(isNano, handle), doGetRemotePort(isNano, handle));
    } finally {
      release();
    }
  }
  public String localId()
  {
    try {
      addRef();
      return doGetLocalId(isNano, handle);
    } finally {
      release();
    }
  }

  public String remoteId()
  {
    try {
      addRef();
      return doGetRemoteId(isNano, handle);
    } finally {
      release();
    }
  }

  private static String toString(byte[] utf8)
  {
    try {
      return new String(utf8, "UTF-8");
    } catch (UnsupportedEncodingException ex) {
      throw new RuntimeException(ex);
    }
  }

  protected JniChannel(String id, boolean isNano)
  {
    super(id);
    this.isNano = isNano;
  }

  private void destroy()
  {
    while (true) {
      long tmp = handle;
      if (handleUpdater.compareAndSet(this, tmp, Long.MIN_VALUE)) {
        try {
          if (tmp != Long.MIN_VALUE) {
            doDestroy(isNano, this, tmp);
          }
        } finally {
          break;
        }
      }
    }
  }

  protected void addRef()
  {
    if (refcount.getAndIncrement() == 0) {
      throw new RuntimeException("Calling addRef on Channel that has no valid refcount");
    }
  }

  protected void release()
  {
    if (refcount.decrementAndGet() == 0) {
      destroy();
    }
  }

  public Future shutdown()
  {
    release();
    return CompletedFuture.instance();
  }

  public Future response(long id, int code, KeyValuePair[] headers, byte[] payload, int offset, int size)
  {
    try {
      addRef();
      doResponse(isNano, handle, id, code, headers, payload, offset, size);
      return CompletedFuture.instance();
    } finally {
      release();
    }
  }

  protected long doRequest(int code, KeyValuePair[] headers, byte[] payload, int offset, int size)
  {
    try {
      addRef();
      return doRequest(isNano, handle, code, headers, payload, offset, size);
    } finally {
      release();
    }
  }

  public Future request(int code, KeyValuePair[] headers, byte[] payload, int offset, int size)
  {
    doRequest(code, headers, payload, offset, size);
    return CompletedFuture.instance();
  }
}
