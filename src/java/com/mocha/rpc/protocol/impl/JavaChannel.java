package com.mocha.rpc.protocol.impl;

import com.mocha.rpc.protocol.*;
import static com.mocha.core.exception.Suppressor.*;

import java.net.*;
import java.util.*;
import java.util.zip.*;
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

public class JavaChannel extends ChannelImpl
{
  private final static Logger logger = LoggerFactory.getLogger(com.mocha.rpc.protocol.Channel.class);
  private final static int MAX_PENDING_EVENTS = 128;
  private final static int COMPRESS_SIZE_THRESHOLD = 512;

  private static EventLoopGroupFactory listenLoopGroupFactory = new EventLoopGroupFactory(NioEventLoopGroup.class);
  private static EventLoopGroupFactory socketLoopGroupFactory = listenLoopGroupFactory;
  private final AtomicLong lastKeepaliveTime = new AtomicLong(0);

  private int flags;
  private int version;

  private boolean acceptZlib = false;

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
    JavaChannel.listenLoopGroupFactory = factory;
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
    JavaChannel.socketLoopGroupFactory = factory;
  }

  private io.netty.channel.Channel channel;
  private AtomicLong currentId = new AtomicLong();
  private volatile ByteOrder order;
  private ByteOrder localOrder = ByteOrder.nativeOrder();
  private Thread dispatcher;
  private BlockingQueue pendingEvents;
  private volatile boolean running;

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

  protected JavaChannel(String id, boolean dispatchThread)
  {
    super(id);
    if (dispatchThread) {
      initDispatcher();
    }
  }

  protected void init(io.netty.channel.Channel channel)
  {
    this.channel = channel;
    super.init((InetSocketAddress) channel.localAddress(), (InetSocketAddress) channel.remoteAddress());
  }

  protected Future writeAndFlush(ByteBuf buf)
  {
    return channel.writeAndFlush(buf);
  }

  protected Future keepalive(long interval)
  {
    long now = System.currentTimeMillis();
    if (now - lastKeepaliveTime.get() < interval) {
      return CompletedFuture.instance();
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

  private int compressBound(int inputSize)
  {
    return inputSize + (inputSize >> 12) + (inputSize >> 14) + (inputSize >> 25) + 13;
  }

  private ByteBuf compress(ByteBuf input, int inputSize)
  {
    ByteBuf output = Unpooled.buffer(compressBound(inputSize) + 4).order(order);
    output.writeInt(inputSize);
    try {
      try (ByteBufOutputStream bos = new ByteBufOutputStream(output);
          DeflaterOutputStream dos = new DeflaterOutputStream(bos)) {
        input.readBytes(dos, inputSize);
      }
      int compressedSize = output.readableBytes();
      /* TODO make these configurable */
      if (compressedSize < inputSize * 0.90 || inputSize - compressedSize > 128) {
        logger.info("Compressed size " + compressedSize + " is sufficiently less than original input size " + inputSize + ", use compressed version");
        return output;
      } else {
        logger.info("Compressed size " + compressedSize + " is not saving significant amount of data compared to original input size " + inputSize + ", use plain version");
        output.release();
        return input;
      }
    } catch (IOException ex) {
      throw new RuntimeException(ex);
    }
  }

  public Future doSend(long id, int code, int type, KeyValuePair[] headers, byte[] payload, int offset, int size)
  {
    if (getRemoteId() == null) {
      throw new IllegalStateException("Session has not been established yet");
    }
    int headersSize;
    ByteBuf serializedHeaders;
    int flags = 0;
    if (headers != null && headers.length > 0) {
      serializedHeaders = ProtocolUtils.serialize(headers, localOrder);
      headersSize = serializedHeaders.readableBytes();
      if (acceptZlib && headersSize > COMPRESS_SIZE_THRESHOLD) {
        ByteBuf compressed = compress(serializedHeaders, headersSize);
        if (serializedHeaders != compressed) {
          serializedHeaders.release();
          serializedHeaders = compressed;
          headersSize = compressed.readableBytes();
          flags = RPCHandler.PACKET_FLAG_HEADER_ZLIB_ENCODED;
        }
      }
    } else {
      serializedHeaders = null;
      headersSize = 0;
    }
    ByteBuf payloadBuffer;
    if (size > 0) {
      if (payload.length < offset + size) {
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
    fixedBuffer.writeInt(type | (flags << 8));
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

  public Future response(long id, int code, KeyValuePair[] headers, byte[] payload, int offset, int size)
  {
    return doSend(id, code, RPCHandler.PACKET_TYPE_RESPONSE, headers, payload, offset, size);
  }

  public Future request(int code, KeyValuePair[] headers, byte[] payload, int offset, int size)
  {
    return doSend(currentId.getAndIncrement(), code, RPCHandler.PACKET_TYPE_REQUEST, headers, payload, offset, size);
  }

  public Future shutdown()
  {
    if (channel != null) {
      if (getRemoteId() ==  null) {
        remoteId("NOT_AVAILABLE");
      }
      return channel.close();
    } else {
      return CompletedFuture.instance();
    }
  }

  protected void remoteOrder(ByteOrder order)
  {
    this.order = order;
    synchronized (this) {
      notifyAll();
    }
  }

  protected void flags(int flags)
  {
    this.flags = flags;
    this.acceptZlib = (flags & RPCHandler.NEGOTIATION_FLAG_ACCEPT_ZLIB) == RPCHandler.NEGOTIATION_FLAG_ACCEPT_ZLIB;
  }

  protected void version(int version)
  {
    this.version = version;
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
