package com.moca.rpc.protocol.impl;

import com.moca.rpc.protocol.*;
import static com.moca.core.exception.Suppressor.*;

import java.net.*;
import java.util.*;
import java.util.concurrent.atomic.*;
import java.util.concurrent.*;
import java.nio.*;
import java.io.*;

import org.slf4j.*;

import io.netty.buffer.*;
import io.netty.channel.*;
import io.netty.channel.nio.*;
import io.netty.handler.ssl.*;
import io.netty.handler.ssl.util.*;

public class ChannelImpl extends com.moca.rpc.protocol.Channel
{
  private static Logger logger = LoggerFactory.getLogger(ChannelImpl.class);
  protected static int MAX_CHANNEL_ID_LENGTH = 128;

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
    private boolean isClient = false;
    private InetSocketAddress address;
    private ChannelListener listener;
    private int timeout = 10;
    private int limit = Integer.MAX_VALUE;
    private boolean debug = false;
    private SslContext ssl;
    private byte[] id;

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

    public Builder connect(String address)
    {
      isClient = true;
      setAddress(address);
      return this;
    }

    public Builder bind(String address)
    {
      isClient = false;
      setAddress(address);
      return this;
    }

    public Builder listener(ChannelListener listener)
    {
      this.listener = checkNotNull(listener);
      return this;
    }

    public Builder debug()
    {
      this.debug = true;
      return this;
    }

    public Builder ssl()
    {
      SelfSignedCertificate ssc = run(() -> new SelfSignedCertificate());
      return ssl(ssc.certificate(), ssc.privateKey());
    }

    public Builder ssl(File certificateChainFile, File privateKey)
    {
      this.ssl = run(() -> SslContext.newServerContext(certificateChainFile, privateKey));
      return this;
    }

    public Builder ssl(File certificateChainFile, File privateKey, String keyPassword)
    {
      this.ssl = run(() -> SslContext.newServerContext(certificateChainFile, privateKey, keyPassword));
      return this;
    }

    public Builder ssl(File certificateChainFile, File privateKey, Callable<String> keyPasswordProvider)
    {
      this.ssl = run(() -> SslContext.newServerContext(certificateChainFile, privateKey, keyPasswordProvider.call()));
      return this;
    }

    public Builder id(String id)
    {
      this.id = run(() -> checkNotEmpty(id).getBytes("UTF-8"));
      if (this.id.length > MAX_CHANNEL_ID_LENGTH) {
        throw new RuntimeException("Channel ID length " + this.id.length + " is greater than max limit of " + MAX_CHANNEL_ID_LENGTH);
      }
      return this;
    }

    public com.moca.rpc.protocol.Channel build()
    {
      while (id == null) {
        id(UUID.randomUUID().toString());
      }
      if (isClient) {
        return run(() -> new ClientChannel(new String(id, "UTF-8"), listener, address, 50 * 60, limit, debug, ssl));
      } else {
        return run(() -> new ServerChannel(new String(id, "UTF-8"), listener, address, 50 * 60, limit, debug, ssl));
      }
    }
  }

  private io.netty.channel.Channel channel;
  private AtomicLong currentId = new AtomicLong();
  private volatile ByteOrder order;
  private ByteOrder localOrder = ByteOrder.nativeOrder();
  private String localId;
  private volatile String remoteId;

  public static Builder builder()
  {
    return new Builder();
  }

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
    if (remoteId == null) {
      synchronized (this) {
        while (remoteId == null) {
          try {
            wait();
          } catch (InterruptedException ex) {
            throw new RuntimeException(ex);
          }
        }
      }
    }
    return remoteId;
  }

  protected void init(io.netty.channel.Channel channel)
  {
    this.channel = channel;
  }

  protected Future writeAndFlush(ByteBuf buf)
  {
    return channel.writeAndFlush(buf);
  }

  public Future doSend(long id, Map<String, String> response)
  {
    ByteBuf serialized = ProtocolUtils.serialize(response, localOrder);
    int serializedSize = serialized.readableBytes();
    ByteBuf fixedBuffer = Unpooled.buffer(8 + 4 + 4 + 4).order(localOrder);
    // request id
    fixedBuffer.writeLong(id);
    //flag
    fixedBuffer.writeInt(0);
    //header size
    fixedBuffer.writeInt(serializedSize);
    //payload size
    fixedBuffer.writeInt(0);
    return writeAndFlush(Unpooled.wrappedBuffer(fixedBuffer, serialized));
  }

  public Future response(long id, Map<String, String> response)
  {
    return doSend(id, response);
  }

  public Future request(Map<String, String> request)
  {
    return doSend(currentId.getAndIncrement(), request);
  }

  public Future shutdown()
  {
    if (channel != null) {
      if (remoteId == null) {
        remoteId = "NOT_AVAILABLE";
      }
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

  protected void remoteId(String id)
  {
    this.remoteId = id;
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
