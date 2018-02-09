package com.mocha.rpc.protocol.impl;

import com.mocha.rpc.protocol.*;

import java.net.*;
import java.util.concurrent.*;

public class JniChannelEasy extends JniChannel implements ChannelEasy
{
  public JniChannelEasy(String id, InetSocketAddress address, long timeout, long keepalive)
  {
    super(id, false);
    jniInit(address, timeout, keepalive);
    init(localAddress(), remoteAddress());
    remoteId(remoteId());
  }

  private native static Packet doPoll(long handle);
  private native static void doInterrupt(long handle);

  public Packet poll()
  {
    try {
      addRef();
      return doPoll(handle);
    } finally {
      release();
    }
  }

  public Future request(int code, KeyValuePair[] headers, byte[] payload, int offset, int size)
  {
    final long requestId = doRequest(code, headers, payload, offset, size);
    return new Future() {
      public boolean cancel(boolean mayInterruptIfRunning)
      {
        return false;
      }

      public boolean isCancelled()
      {
        return false;
      }

      public boolean isDone()
      {
        return true;
      }

      public Object get(long timeout, TimeUnit unit)
      {
        return get();
      }

      public Object get()
      {
        Packet packet = null;
        do {
          packet = poll();
        } while (!packet.isResponse() || packet.id() != requestId);

        return packet;
      }
    };
  }

  public void interrupt()
  {
    try {
      addRef();
      doInterrupt(handle);
    } finally {
      release();
    }
  }
}
