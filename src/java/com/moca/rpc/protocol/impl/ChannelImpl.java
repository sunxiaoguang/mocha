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
  private static int MAX_PENDING_EVENTS = 128;

  private static EventLoopGroupFactory listenLoopGroupFactory = new EventLoopGroupFactory(NioEventLoopGroup.class);
  private static EventLoopGroupFactory socketLoopGroupFactory = listenLoopGroupFactory;
  private AtomicLong lastKeepaliveTime = new AtomicLong(0);

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
    private int keepaliveInterval = 5;
    private byte[] id;
    private boolean enableDispatchThread = false;

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

    public Builder keepalive(long interval, TimeUnit unit)
    {
      this.keepaliveInterval = (int) TimeUnit.SECONDS.convert(interval, unit);
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

    public Builder dispatchThread()
    {
      this.enableDispatchThread = true;
      return this;
    }

    public com.moca.rpc.protocol.Channel build()
    {
      while (id == null) {
        id(UUID.randomUUID().toString());
      }
      if (isClient) {
        return run(() -> new ClientChannel(new String(id, "UTF-8"), listener, address, timeout, keepaliveInterval, limit, debug, ssl));
      } else {
        return run(() -> new ServerChannel(new String(id, "UTF-8"), listener, address, timeout, keepaliveInterval, limit, debug, ssl));
      }
    }
  }

  private io.netty.channel.Channel channel;
  private AtomicLong currentId = new AtomicLong();
  private volatile ByteOrder order;
  private ByteOrder localOrder = ByteOrder.nativeOrder();
  private String localId;
  private volatile String remoteId;
  private Thread dispatcher;
  private BlockingQueue pendingEvents;
  private volatile boolean running;

  public static Builder builder()
  {
    return new Builder();
  }

  private void doDispatch()
  {
  }

  private void initDispatcher()
  {
    pendingEvents = new ArrayBlockingQueue(MAX_PENDING_EVENTS);
    running = true;
    dispatcher = new Thread(() -> {
      doDispatch();
    });
    dispatcher.start();
  }

  protected ChannelImpl(String id, boolean dispatchThread)
  {
    this.localId = id;
    if (dispatchThread) {
      initDispatcher();
    }
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

  protected Future keepalive(long interval)
  {
    long now = System.currentTimeMillis();
    if (now - lastKeepaliveTime.get() < interval) {
      return RPCFuture.completed();
    }
    lastKeepaliveTime.set(now);
    ByteBuf fixedBuffer = Unpooled.buffer(8 + 4 + 4 + 4 + 4).order(localOrder);
    // request id
    fixedBuffer.writeLong(currentId.getAndIncrement());
    // code
    fixedBuffer.writeInt(RPCHandler.HINT_CODE_KEEPALIVE);
    // flag
    fixedBuffer.writeInt(RPCHandler.PACKET_TYPE_HINT);
    // header size
    fixedBuffer.writeInt(0);
    // payload size
    fixedBuffer.writeInt(0);
    return writeAndFlush(fixedBuffer);
  }

  public Future doSend(long id, int code, int type, Map<String, String> headers, byte[] payload, int offset, int size)
  {
    if (remoteId == null) {
      throw new IllegalStateException("Session has not been established yet");
    }
    int headersSize;
    ByteBuf serializedHeaders;
    if (headers != null && !headers.isEmpty()) {
      serializedHeaders = ProtocolUtils.serialize(headers, localOrder);
      headersSize = serializedHeaders.readableBytes();
    } else {
      serializedHeaders = null;
      headersSize = 0;
    }
    ByteBuf payloadBuffer;
    if (size > 0) {
      if (payload.length <= offset + size) {
        throw new ArrayIndexOutOfBoundsException(offset + size);
      }
      payloadBuffer = Unpooled.wrappedBuffer(payload, offset, size);
    } else {
      payloadBuffer = null;
    }
    ByteBuf fixedBuffer = Unpooled.buffer(8 + 4 + 4 + 4 + 4).order(localOrder);
    // request id
    fixedBuffer.writeLong(id);
    // code
    fixedBuffer.writeInt(code);
    // flag
    fixedBuffer.writeInt(type);
    // header size
    fixedBuffer.writeInt(headersSize);
    // payload size
    fixedBuffer.writeInt(size);
    ByteBuf buffer = fixedBuffer;
    if (headersSize > 0) {
      buffer = Unpooled.wrappedBuffer(buffer, serializedHeaders);
    }
    if (size > 0) {
      buffer = Unpooled.wrappedBuffer(buffer, payloadBuffer);
    }
    return writeAndFlush(buffer);
  }

  public Future response(long id, int code, Map<String, String> headers, byte[] payload, int offset, int size)
  {
    return doSend(id, code, RPCHandler.PACKET_TYPE_RESPONSE, headers, payload, offset, size);
  }

  public Future request(int code, Map<String, String> headers, byte[] payload, int offset, int size)
  {
    return doSend(currentId.getAndIncrement(), code, RPCHandler.PACKET_TYPE_REQUEST, headers, payload, offset, size);
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
