package com.moca.rpc.protocol.impl;

import com.moca.rpc.protocol.*;

import java.net.*;
import java.util.*;
import java.util.concurrent.atomic.*;
import java.util.concurrent.*;
import java.nio.*;

import org.slf4j.*;

import io.netty.buffer.*;
import io.netty.channel.*;
import io.netty.channel.nio.*;

public class ChannelImpl extends com.moca.rpc.protocol.Channel
{
  private static Logger logger = LoggerFactory.getLogger(ChannelImpl.class);

  private static EventLoopGroupFactory listenLoopGroupFactory = new EventLoopGroupFactory(NioEventLoopGroup.class);
  private static EventLoopGroupFactory socketLoopGroupFactory = listenLoopGroupFactory;

  static synchronized EventLoopGroup createListenEventLoopGroup()
  {
    return listenLoopGroupFactory.create();
  }

  static synchronized Future stopListenEventLoopGroup(EventLoopGroup loopGroup)
  {
    return listenLoopGroupFactory.shutdown(loopGroup);
  }

  public static synchronized void listenEventLoopGroupFactory(EventLoopGroupFactory factory)
  {
    ChannelImpl.listenLoopGroupFactory = factory;
  }

  static synchronized EventLoopGroup createSocketEventLoopGroup()
  {
    return socketLoopGroupFactory.create();
  }

  static synchronized Future stopSocketEventLoopGroup(EventLoopGroup loopGroup)
  {
    return socketLoopGroupFactory.shutdown(loopGroup);
  }

  public static synchronized void socketEventLoopGroupFactory(EventLoopGroupFactory factory)
  {
    ChannelImpl.socketLoopGroupFactory = factory;
  }

  public static final class Builder implements com.moca.rpc.protocol.Channel.Builder
  {
    private InetSocketAddress address;
    private ChannelListener listener;
    private int timeout = 10;
    private int limit = Integer.MAX_VALUE;

    private Builder()
    {
    }

    private <T> T checkNotNull(T arg)
    {
      if (arg == null) {
        throw new IllegalArgumentException("Argument is null");
      } else {
        return arg;
      }
    }

    private void setAddress(String address)
    {
      String parts[] = checkNotNull(address).split(":");
      if (parts.length != 2) {
        throw new IllegalArgumentException("Invalid address " + address);
      }
      this.address = new InetSocketAddress(parts[0], Integer.parseInt(parts[1]));
    }

    public Builder timeout(long timeout, TimeUnit unit)
    {
      this.timeout = (int) TimeUnit.SECONDS.convert(timeout, unit);
      return this;
    }

    public Builder limit(int limit)
    {
      if (limit > 0) {
        this.limit = limit;
      } else {
        this.limit = Integer.MAX_VALUE;
      }
      return this;
    }

    public Builder bind(String address)
    {
      setAddress(address);
      return this;
    }

    public Builder listener(ChannelListener listener)
    {
      this.listener = checkNotNull(listener);
      return this;
    }

    public com.moca.rpc.protocol.Channel build()
    {
      return new ServerChannel(listener, address, 50 * 60, limit);
    }
  }

  private io.netty.channel.Channel channel;
  private AtomicInteger currentId = new AtomicInteger();
  private volatile ByteOrder order;

  public static Builder builder()
  {
    return new Builder();
  }

  protected ChannelImpl()
  {
  }

  protected void init(io.netty.channel.Channel channel)
  {
    this.channel = channel;
  }

  public Future response(Map<String, String> response)
  {
    ByteBuf serialized = ProtocolUtils.serialize(response, order);
    int serializedSize = serialized.readableBytes();
    ByteBuf fixedBuffer = Unpooled.buffer(4 + 4 + 4).order(order);
    //flag
    fixedBuffer.writeInt(0);
    //header size
    fixedBuffer.writeInt(serializedSize);
    //payload size
    fixedBuffer.writeInt(0);
    return channel.writeAndFlush(Unpooled.wrappedBuffer(fixedBuffer, serialized));
  }

  public Future shutdown()
  {
    if (channel != null) {
      return channel.close();
    } else {
      return RPCFuture.completed();
    }
  }

  public InetSocketAddress getLocalAddress()
  {
    return (InetSocketAddress) channel.localAddress();
  }

  public InetSocketAddress getRemoteAddress()
  {
    return (InetSocketAddress) channel.remoteAddress();
  }

  protected void remoteOrder(ByteOrder order)
  {
    this.order = order;
    synchronized (this) {
      notifyAll();
    }
  }

  public ByteOrder remoteOrder()
  {
    if (order == null) {
      synchronized (this) {
        while (order == null) {
          try {
            wait();
          } catch (InterruptedException ex) {
            throw new RuntimeException(ex);
          }
        }
      }
    }
    return order;
  }

  public ByteOrder localOrder()
  {
    return ProtocolUtils.NATIVE_ORDER;
  }
}
