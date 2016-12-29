package com.moca.rpc.protocol;

import com.moca.rpc.protocol.impl.*;

import java.util.*;
import java.util.concurrent.*;
import java.net.*;
import java.lang.reflect.*;

public final class ChannelBuilder
{
  private boolean isClient = false;
  private InetSocketAddress address;
  private ChannelListener listener;
  private int timeout = 10;
  private int limit = Integer.MAX_VALUE;
  private boolean debug = false;
  private int keepaliveInterval = 5;
  private byte[] id;
  private boolean enableDispatchThread = false;
  private static Constructor clientChannelConstructor = null;
  private static Constructor serverChannelConstructor = null;

  static {
    try {
      clientChannelConstructor = Class.forName("com.moca.rpc.protocol.impl.ClientChannel")
        .getDeclaredConstructor(String.class, ChannelListener.class, InetSocketAddress.class, int.class, int.class, int.class, boolean.class, Object.class);
    } catch (Throwable ignored) {
    }
    try {
      serverChannelConstructor = Class.forName("com.moca.rpc.protocol.impl.ServerChannel")
        .getDeclaredConstructor(String.class, ChannelListener.class, InetSocketAddress.class, int.class, int.class, int.class, boolean.class, Object.class);
    } catch (Throwable ignored) {
    }
  }

  /*
  private SslContext ssl;
  */

  private <T> T checkNotNull(T arg)
  {
    if (arg == null) {
      throw new IllegalArgumentException("Argument is null");
    } else {
      return arg;
    }
  }

  private String checkNotEmpty(String str)
  {
    str = checkNotNull(str);
    if (str.isEmpty()) {
      throw new IllegalArgumentException("Argument is empty");
    }
    return str;
  }

  private void setAddress(String address)
  {
    String parts[] = checkNotNull(address).split(":");
    if (parts.length != 2) {
      throw new IllegalArgumentException("Invalid address " + address);
    }
    this.address = new InetSocketAddress(parts[0], Integer.parseInt(parts[1]));
  }

  public ChannelBuilder timeout(long timeout, TimeUnit unit)
  {
    this.timeout = (int) TimeUnit.SECONDS.convert(timeout, unit);
    return this;
  }

  public ChannelBuilder limit(int limit)
  {
    if (limit > 0) {
      this.limit = limit;
    } else {
      this.limit = Integer.MAX_VALUE;
    }
    return this;
  }

  public ChannelBuilder connect(String address)
  {
    isClient = true;
    setAddress(address);
    return this;
  }

  public ChannelBuilder bind(String address)
  {
    isClient = false;
    setAddress(address);
    return this;
  }

  public ChannelBuilder listener(ChannelListener listener)
  {
    this.listener = checkNotNull(listener);
    return this;
  }

  public ChannelBuilder keepalive(long interval, TimeUnit unit)
  {
    this.keepaliveInterval = (int) TimeUnit.SECONDS.convert(interval, unit);
    return this;
  }

  public ChannelBuilder debug()
  {
    this.debug = true;
    return this;
  }

  /*
  public ChannelBuilder ssl()
  {
    SelfSignedCertificate ssc = run(() -> new SelfSignedCertificate());
    return ssl(ssc.certificate(), ssc.privateKey());
  }

  public ChannelBuilder ssl(File certificateChainFile, File privateKey)
  {
    this.ssl = run(() -> SslContext.newServerContext(certificateChainFile, privateKey));
    return this;
  }

  public ChannelBuilder ssl(File certificateChainFile, File privateKey, String keyPassword)
  {
    this.ssl = run(() -> SslContext.newServerContext(certificateChainFile, privateKey, keyPassword));
    return this;
  }

  public ChannelBuilder ssl(File certificateChainFile, File privateKey, Callable<String> keyPasswordProvider)
  {
    this.ssl = run(() -> SslContext.newServerContext(certificateChainFile, privateKey, keyPasswordProvider.call()));
    return this;
  }

  public ChannelBuilder dispatchThread()
  {
    this.enableDispatchThread = true;
    return this;
  }
  */

  public ChannelBuilder id(String id)
  {
    this.id = ChannelImpl.toUtf8(checkNotEmpty(id));
    if (this.id.length > ChannelImpl.MAX_CHANNEL_ID_LENGTH) {
      throw new RuntimeException("Channel ID length " + this.id.length + " is greater than max limit of " + ChannelImpl.MAX_CHANNEL_ID_LENGTH);
    }
    return this;
  }

  private void preconditions()
  {
    while (id == null) {
      id(UUID.randomUUID().toString());
    }
    if (address == null) {
      throw new IllegalStateException("Address is not set");
    }
  }

  public Channel build()
  {
    preconditions();
    try {
      if (isClient) {
        return (Channel) clientChannelConstructor.newInstance(ChannelImpl.fromUtf8(id), listener, address, timeout, keepaliveInterval, limit, debug, null);
      } else {
        return (Channel) serverChannelConstructor.newInstance(ChannelImpl.fromUtf8(id), listener, address, timeout, keepaliveInterval, limit, debug, null);
      }
    } catch (Throwable cause) {
      throw new RuntimeException(cause);
    }
  }

  public ChannelNano nano()
  {
    preconditions();
    if (!isClient) {
      throw new IllegalStateException("Can't bind with nano channel");
    }
    JniChannelNano rpc = null;
    JniChannelNano result;
    try {
      rpc = new JniChannelNano(ChannelImpl.fromUtf8(id), address, timeout * 1000000l, keepaliveInterval * 1000000l, listener);
      result = rpc;
      rpc = null;
      return result;
    } finally {
      if (rpc != null) {
        try {
          rpc.shutdown();
        } catch (Exception ex) {
          throw new RuntimeException(ex);
        }
      }
    }
  }

  public ChannelEasy easy()
  {
    preconditions();
    if (!isClient) {
      throw new IllegalStateException("Can't bind with easy channel");
    }
    JniChannelEasy rpc = null;
    JniChannelEasy result;
    try {
      rpc = new JniChannelEasy(ChannelImpl.fromUtf8(id), address, timeout * 1000000l, keepaliveInterval * 1000000l);
      result = rpc;
      rpc = null;
      return result;
    } finally {
      if (rpc != null) {
        try {
          rpc.shutdown();
        } catch (Exception ex) {
          throw new RuntimeException(ex);
        }
      }
    }
  }
}
