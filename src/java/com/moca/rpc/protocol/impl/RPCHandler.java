package com.moca.rpc.protocol.impl;

import java.nio.*;
import java.util.*;
import java.io.*;

import io.netty.buffer.*;
import io.netty.channel.*;

import org.slf4j.*;

import com.moca.rpc.protocol.*;
import static com.moca.core.exception.Suppressor.*;

public class RPCHandler extends ChannelInboundHandlerAdapter
{
  private static Logger logger = LoggerFactory.getLogger(RPCHandler.class);

  private static final byte STATE_NEGOTIATION = 0;
  private static final byte STATE_HEADER = 1;
  private static final byte STATE_PAYLOAD = 2;
  private static final byte STATE_START_OVER = 3;
  private byte handlerState = STATE_NEGOTIATION;
  private NegotiationReader negotiationReader = new NegotiationReader();
  private HeaderReader headerReader = new HeaderReader();
  private PayloadReader payloadReader = new PayloadReader();
  private ChannelImpl channel;
  private ChannelListener listener;
  private ByteBuf queued;
  private int numberOfQueuedComponents;
  private int minReadableSize;
  private int limit;
  private ByteOrder channelOrder;
  private int channelFlags;
  private int channelVersion;
  private boolean sendNegotiation = false;

  private void setMinReadableBytes(int bytes)
  {
    this.minReadableSize = bytes;
  }

  private int getMinReadableBytes()
  {
    return minReadableSize;
  }

  private class Reader
  {
    private byte readerState;

    protected void reset(byte state)
    {
      setMinReadableBytes(1);
      this.readerState = state;
    }

    protected int state()
    {
      return readerState;
    }

    protected long readLong(ByteBuf buffer, byte nextState)
    {
      int readable = buffer.readableBytes();
      long result = 0;
      if (readable >= 8) {
        result = buffer.readLong();
        readerState = nextState;
        setMinReadableBytes(1);
      } else {
        setMinReadableBytes(8);
      }

      return result;
    }

    protected int readInt(ByteBuf buffer, byte nextState)
    {
      int readable = buffer.readableBytes();
      int result = 0;
      if (readable >= 4) {
        result = buffer.readInt();
        readerState = nextState;
        setMinReadableBytes(1);
      } else {
        setMinReadableBytes(4);
      }

      return result;
    }

    protected boolean isState(int state)
    {
      return this.readerState == state;
    }

    void readBits(ByteBuf input, ByteBuf buffer, int size, byte nextState)
    {
      int available = buffer.readableBytes();
      int readable = input.readableBytes();
      int read = size - available;
      if (read > readable) {
        read = readable;
      }
      input.readBytes(buffer, read);
      if (read + available == size) {
        readerState = nextState;
      }
    }
  }

  private final class NegotiationReader extends Reader
  {
    private static final byte STATE_READ_MAGIC = 0;
    private static final byte STATE_READ_FLAGS = 1;
    private static final byte STATE_READ_PEER_ID_LENGTH = 2;
    private static final byte STATE_READ_PEER_ID = 3;
    private static final byte STATE_READ_DONE = 4;

    private ByteOrder order;
    private int flags;
    private int version;
    private int peerIdLength;
    private ByteBuf peerId;

    private int getFlags()
    {
      return flags;
    }

    private int getVersion()
    {
      return version;
    }

    private String getId()
    {
      return ProtocolUtils.readString(peerId);
    }

    private NegotiationReader() {
      reset(STATE_READ_MAGIC);
    }

    private ByteBuf readMagic(ByteBuf buffer)
    {
      if (buffer.readableBytes() >= 4) {
        byte[] array = new byte[4];
        buffer.readBytes(array);
        String magicCode = new String(array);
        switch (magicCode) {
          case "ACOM":
            order = ByteOrder.LITTLE_ENDIAN;
            break;
          case "MOCA":
            order = ByteOrder.BIG_ENDIAN;
            break;
          default:
            throw new RuntimeException("Invalid magic code" + magicCode);
        }
        reset(STATE_READ_FLAGS);
      } else {
        setMinReadableBytes(4);
      }
      return buffer;
    }

    private ByteBuf readFlags(ByteBuf buffer)
    {
      buffer = buffer.order(order);
      flags = readInt(buffer, STATE_READ_PEER_ID_LENGTH);
      if (isState(STATE_READ_PEER_ID_LENGTH)) {
        version = flags & 0xFF;
        flags >>>= 8;
      }
      return buffer;
    }

    private ByteBuf readPeerIdLength(ByteBuf buffer)
    {
      buffer = buffer.order(order);
      peerIdLength = readInt(buffer, STATE_READ_PEER_ID);
      if (isState(STATE_READ_PEER_ID)) {
        if (peerIdLength > ChannelImpl.MAX_CHANNEL_ID_LENGTH) {
          throw new RuntimeException("Peer ID length " + peerIdLength + " is greater than max limit of " + ChannelImpl.MAX_CHANNEL_ID_LENGTH);
        }
        peerId = Unpooled.buffer(peerIdLength);
      }
      return buffer;
    }

    private ByteBuf readPeerId(ByteBuf buffer)
    {
      readBits(buffer, peerId, peerIdLength, STATE_READ_DONE);
      return buffer;
    }

    private ByteBuf doRead(ByteBuf buffer)
    {
      switch (state()) {
        case STATE_READ_MAGIC:
          buffer = readMagic(buffer);
          break;
        case STATE_READ_FLAGS:
          buffer = readFlags(buffer);
          break;
        case STATE_READ_PEER_ID_LENGTH:
          buffer = readPeerIdLength(buffer);
          break;
        case STATE_READ_PEER_ID:
          buffer = readPeerId(buffer);
        default:
      }
      return buffer;
    }

    ByteOrder read(ByteBuf buffer)
    {
      while (buffer.readableBytes() >= getMinReadableBytes() && !isState(STATE_READ_DONE)) {
        buffer = doRead(buffer);
      }
      if (isState(STATE_READ_DONE)) {
        return order;
      } else {
        return null;
      }
    }
  }

  private final class HeaderReader extends Reader
  {
    private static final byte STATE_READ_HEADER_REQUEST_ID = 0;
    private static final byte STATE_READ_HEADER_FLAGS = 1;
    private static final byte STATE_READ_HEADER_SIZE = 2;
    private static final byte STATE_READ_PAYLOAD_SIZE = 3;
    private static final byte STATE_READ_HEADER = 4;
    private static final byte STATE_READ_DONE = 5;

    private long requestId;
    private int flags;
    private int headerSize;
    private int payloadSize;
    ByteBuf headerBuffer;
    private long recvSize;

    HeaderReader() {
      reset();
    }

    void reset()
    {
      reset(STATE_READ_HEADER_REQUEST_ID);
      headerBuffer = null;
    }

    void readHeaderRequestId(ByteBuf buffer)
    {
      requestId = readLong(buffer, STATE_READ_HEADER_FLAGS);
    }

    void readHeaderFlags(ByteBuf buffer)
    {
      flags = readInt(buffer, STATE_READ_HEADER_SIZE);
    }

    void readHeaderSize(ByteBuf buffer)
    {
      headerSize = readInt(buffer, STATE_READ_PAYLOAD_SIZE);
    }

    void readPayloadSize(ByteBuf buffer)
    {
      payloadSize = readInt(buffer, STATE_READ_HEADER);
      if (isState(STATE_READ_HEADER)) {
        if (headerSize < 0 || payloadSize < 0) {
          throw new RuntimeException("Invalid negative header or payload size");
        }
        if (headerSize > limit) {
          throw new RuntimeException("Header size " + headerSize
              + " from channel " + channel
              + " is larger than limit, force closing");
        }
        if (headerSize == 0) {
          reset(STATE_READ_DONE);
        } else {
          headerBuffer = Unpooled.buffer(headerSize);
        }
      }
    }

    void readHeader(ByteBuf buffer)
    {
      readBits(buffer, headerBuffer, headerSize, STATE_READ_DONE);
    }

    void doRead(ByteBuf buffer)
    {
      switch (state()) {
        case STATE_READ_HEADER_REQUEST_ID:
          readHeaderRequestId(buffer);
          break;
        case STATE_READ_HEADER_FLAGS:
          readHeaderFlags(buffer);
          break;
        case STATE_READ_HEADER_SIZE:
          readHeaderSize(buffer);
          break;
        case STATE_READ_PAYLOAD_SIZE:
          readPayloadSize(buffer);
          break;
        case STATE_READ_HEADER:
          readHeader(buffer);
          break;
        default:
          break;
      }
    }

    void read(ByteBuf buffer)
    {
      while (buffer.readableBytes() >= getMinReadableBytes() && state() != STATE_READ_DONE) {
        doRead(buffer);
      }
      if (isState(STATE_READ_DONE)) {
        dispatchRequest(requestId, ProtocolUtils.deserialize(headerBuffer, channelOrder), payloadSize);
        if (payloadSize == 0) {
          changeState(STATE_START_OVER);
        } else {
          changeState(STATE_PAYLOAD);
        }
      }
    }

    int getPayloadSize()
    {
      return payloadSize;
    }

    long getRequestId()
    {
      return requestId;
    }
  }

  private final class PayloadReader extends Reader
  {
    private static final byte STATE_READ_PAYLOAD = 0;
    private static final byte STATE_READ_DONE = 1;

    private int remaining;
    private long requestId;

    void reset()
    {
      remaining = headerReader.getPayloadSize();
      requestId = headerReader.getRequestId();
      reset(STATE_READ_PAYLOAD);
    }

    void readPayload(ByteBuf buffer)
    {
      int available = buffer.readableBytes();
      if (available > remaining) {
        buffer = buffer.readBytes(remaining);
        remaining = 0;
      } else {
        remaining -= available;
      }
      boolean commit = remaining == 0;
      try {
        dispatchPayload(requestId, buffer, commit);
      } finally {
        if (commit) {
          reset(STATE_READ_DONE);
        }
      }
    }

    void read(ByteBuf buffer)
    {
      if (isState(STATE_READ_PAYLOAD)) {
        readPayload(buffer);
      }
      if (isState(STATE_READ_DONE)) {
        changeState(STATE_START_OVER);
      }
    }
  }

  public RPCHandler(ChannelImpl channel, ChannelListener listener, int limit, boolean sendNegotiation) {
    this.channel = channel;
    this.listener = listener;
    this.limit = limit;
    this.sendNegotiation = sendNegotiation;
  }

  private void doSendNegotiation()
  {
    String id = channel.getLocalId();
    byte[] idBytes = run(() -> id.getBytes("UTF-8"));
    if (idBytes.length > ChannelImpl.MAX_CHANNEL_ID_LENGTH) {
      throw new RuntimeException("Size of channel id " + idBytes.length + " is greater than limit of " + ChannelImpl.MAX_CHANNEL_ID_LENGTH);
    }
    ByteBuf buffer = Unpooled.buffer(4 + 4 + 4).order(ByteOrder.nativeOrder());
    buffer.writeInt(0X4D4F4341);
    buffer.writeInt(0);
    buffer.writeInt(idBytes.length);
    channel.writeAndFlush(Unpooled.wrappedBuffer(buffer, Unpooled.wrappedBuffer(idBytes)));
  }

  @Override
  public void channelActive(ChannelHandlerContext ctx) throws Exception
  {
    channel.init(ctx.channel());
    if (sendNegotiation) {
      doSendNegotiation();
    }
    listener.onConnect(channel);
  }

  @Override
  public void channelInactive(ChannelHandlerContext ctx) throws Exception
  {
    listener.onDisconnect(channel);
  }

  private ByteBuf read(ByteBuf buffer)
  {
    if (handlerState == STATE_NEGOTIATION) {
      channelOrder = negotiationReader.read(buffer);
      if (channelOrder != null) {
        changeState(STATE_HEADER);
      }
      return buffer;
    }
    buffer = buffer.order(channelOrder);
    switch (handlerState) {
      case STATE_HEADER:
        headerReader.read(buffer);
        break;
      case STATE_PAYLOAD:
        payloadReader.read(buffer);
        break;
      case STATE_START_OVER:
        changeState(STATE_HEADER);
        break;
      default:
        throw new RuntimeException("Illegal Channel State : " + handlerState);
    }

    return buffer;
  }

  @Override
  public void channelRead(ChannelHandlerContext ctx, Object msg)
      throws Exception
  {
    ByteBuf input = (ByteBuf) msg;
    ByteBuf buffer = null;
    boolean releaseBuffer = false;
    try {
      if (queued != null) {
        queued.retain();
        input.retain();
        buffer = Unpooled.wrappedBuffer(queued, input);
        releaseBuffer = true;
      } else {
        buffer = input;
      }

      while (buffer.readableBytes() >= getMinReadableBytes()) {
        read(buffer);
      }
    } finally {
      if (buffer == null) {
        return;
      }
      int leftBytes = buffer.readableBytes();
      ByteBuf oldQueued = queued;
      try {
        if (leftBytes == 0) {
          queued = null;
          numberOfQueuedComponents = 0;
        } else {
          int inputSize = input.readableBytes();
          if (leftBytes > inputSize) {
            int queuedSize = queued.readableBytes();
            if (leftBytes - inputSize < queuedSize) {
              queued.skipBytes(queuedSize + inputSize - leftBytes);
            }
            if (++numberOfQueuedComponents > 16) {
              queued = Unpooled.copiedBuffer(queued);
              numberOfQueuedComponents = 0;
            } else {
              oldQueued = null;
            }

            ByteBuf tmp = queued;
            ByteBuf copy = Unpooled.copiedBuffer(input);
            queued = Unpooled.wrappedBuffer(tmp, copy);
          } else {
            int inputRead = inputSize - leftBytes;
            if (inputRead > 0) {
              queued = Unpooled.copiedBuffer(input.skipBytes(inputRead));
            } else {
              queued = Unpooled.copiedBuffer(input);
            }
            numberOfQueuedComponents = 0;
          }
        }
      } finally {
        if (oldQueued != null && oldQueued != queued) {
          oldQueued.release();
        }
      }
      if (releaseBuffer) {
        buffer.release();
      }
      input.release();
    }
  }

  @Override
  public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause)
      throws Exception
  {
    safeRun(listener, checkedListener -> checkedListener.onError(channel, cause));
    logger.warn("Caught exception on channel " + channel + ", shut it down",
        cause);
    ctx.close();
  }

  private void dispatchRequest(long requestId, Map<String, String> header, int payloadSize)
  {
    listener.onRequest(channel, requestId, header, payloadSize);
  }

  private void dispatchPayload(long requestId, ByteBuf buffer, boolean commit)
  {
    try (PayloadInputStream is = new PayloadInputStream(buffer)) {
      listener.onPayload(channel, requestId, is, commit);
    }
  }

  private static void invalidStateMigration(byte oldState, byte expecting, byte newState)
  {
    throw new RuntimeException("Invalid state migration from " + oldState + " => "
        + newState + ", expecting " + expecting);
  }

  private void checkAndSetState(byte newState, byte expected)
  {
    if (newState != expected) {
      invalidStateMigration(handlerState, expected, newState);
    }
    handlerState = expected;
  }

  private void checkAndSetState(byte newState, byte ... expectedStates)
  {
    for (byte expected : expectedStates) {
      if (newState == expected) {
        handlerState = expected;
        return;
      }
    }
    throw new RuntimeException("Invalid state migration from " + handlerState + " => " + newState);
  }

  private void toNegotiation()
  {
    throw new RuntimeException("Invalid state migration to " + handlerState);
  }

  private void toHeader()
  {
    headerReader.reset();
  }

  private void toPayload()
  {
    payloadReader.reset();
  }

  private void toStartOver()
  {
  }

  private void negotiationTo(byte newState)
  {
    checkAndSetState(newState, STATE_HEADER);
    channel.remoteOrder(channelOrder);
    channel.remoteId(negotiationReader.getId());
    channelFlags = negotiationReader.getFlags();
    channelVersion = negotiationReader.getVersion();
    negotiationReader = null;
    listener.onEstablished(channel);
    toHeader();
  }

  private void headerTo(byte newState)
  {
    checkAndSetState(newState, STATE_PAYLOAD, STATE_START_OVER);
    switch (newState) {
      case STATE_PAYLOAD:
        toPayload();
        break;
      case STATE_START_OVER:
        toStartOver();
        break;
    }
  }

  private void payloadTo(byte newState)
  {
    checkAndSetState(newState, STATE_START_OVER);
    toStartOver();
  }

  private void startOverTo(byte newState)
  {
    checkAndSetState(newState, STATE_HEADER);
    /* more future cleanup goes here */
    toHeader();
  }

  private void changeState(byte newState)
  {
    switch (handlerState) {
      case STATE_NEGOTIATION:
        negotiationTo(newState);
        break;
      case STATE_HEADER:
        headerTo(newState);
        break;
      case STATE_PAYLOAD:
        payloadTo(newState);
        break;
      case STATE_START_OVER:
        startOverTo(newState);
        break;
      default:
        throw new RuntimeException("Invalid state : " + handlerState);
    }
  }
}
