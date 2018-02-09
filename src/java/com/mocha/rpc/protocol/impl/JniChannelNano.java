package com.mocha.rpc.protocol.impl;

import com.mocha.rpc.protocol.*;

import java.net.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

public class JniChannelNano extends JniChannel implements ChannelNano
{
  public static int LOOP_FLAG_ONE_SHOT = 1;
  public static int LOOP_FLAG_ONE_NONBLOCK = 2;
  private ChannelListener listener;
  private ThreadLocal<Boolean> onError = new ThreadLocal<Boolean>() {
    @Override
    public Boolean initialValue() {
      return Boolean.FALSE;
    }
  };

  private native static void doLoop(long handle, int flags);
  private native static void doBreakLoop(long handle);
  private native static void doKeepAlive(long handle);

  private void dispatchRequestEvent(long id, int code, KeyValuePair[] headers, int payloadSize)
  {
    listener.onRequest(this, id, code, headers, payloadSize);
  }
  private void dispatchResponseEvent(long id, int code, KeyValuePair[] headers, int payloadSize)
  {
    listener.onResponse(this, id, code, headers, payloadSize);
  }
  private void dispatchPayloadEvent(long id, int code, byte[] payload, boolean commit)
  {
    listener.onPayload(this, id, code, new ByteArrayInputStream(payload), commit);
  }
  private void dispatchConnectedEvent()
  {
    init(localAddress(), remoteAddress());
    listener.onConnected(this);
  }
  private void dispatchEstablishedEvent()
  {
    remoteId(remoteId());
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

  public JniChannelNano(String id, InetSocketAddress address, long timeout, long keepalive, ChannelListener listener)
  {
    super(id, true);
    this.listener = listener;
    jniInit(address, timeout, keepalive);
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
}
