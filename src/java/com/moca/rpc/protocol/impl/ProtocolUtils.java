package com.moca.rpc.protocol.impl;

import java.util.*;
import java.nio.*;
import java.nio.charset.*;

import io.netty.buffer.*;

import com.moca.rpc.protocol.*;

public final class ProtocolUtils
{
  private static Charset UTF8_CHARSET;

  static {
    UTF8_CHARSET = Charset.forName("UTF-8");
  }

  private static final int DEFAULT_BUFFER_SIZE = 4096;
  public static ByteBuf checkAndCreateBuffer(ArrayList<ByteBuf> buffers, ByteBuf buffer, int size, ByteOrder order)
  {
    if (buffer != null && buffer.maxWritableBytes() >= size) {
      return buffer;
    }
    if (size < DEFAULT_BUFFER_SIZE) {
      size = DEFAULT_BUFFER_SIZE;
    }
    buffer = Unpooled.buffer(size).order(order);
    buffers.add(buffer);
    return buffer;
  }

  static KeyValuePair[] EMPTY_KEY_VALUE_PAIR = new KeyValuePair[0];
  static final ByteOrder NATIVE_ORDER = ByteOrder.nativeOrder();

  static byte[] copy(byte[] input, int offset, int length)
  {
    byte[] copy = new byte[length];
    System.arraycopy(input, offset, copy, 0, length);
    return copy;
  }

  static byte[] copy(ByteBuffer buffer)
  {
    byte[] copy = new byte[buffer.remaining()];
    buffer.mark();
    buffer.get(copy).reset();
    return copy;
  }

  private ProtocolUtils()
  {
  }

  static String readString(ByteBuf buffer)
  {
    return buffer.toString(UTF8_CHARSET);
  }

  static String readString(ByteBuf buffer, int length)
  {
    return buffer.toString(0, length, UTF8_CHARSET);
  }

  static String deserializeString(ByteBuf buffer)
  {
    int stringLength = buffer.readInt();
    String result = readString(buffer.readSlice(stringLength));
    buffer.skipBytes(1);
    return result;
  }

  static ByteBuf serializeString(String str, ArrayList<ByteBuf> buffers, ByteBuf buffer, ByteOrder order)
  {
    byte[] tmp = str.getBytes();
    return serializeString(tmp, buffers, buffer, order);
  }

  static ByteBuf serializeString(byte[] encoded, ArrayList<ByteBuf> buffers, ByteBuf buffer, ByteOrder order)
  {
    buffer = checkAndCreateBuffer(buffers, buffer, 4 + encoded.length + 1, order);
    buffer.writeInt(encoded.length);
    buffer.writeBytes(encoded);
    buffer.writeByte(0);
    return buffer;
  }

  static KeyValuePair[] deserialize(ByteBuf buffer, ByteOrder order)
  {
    if (buffer == null || buffer.readableBytes() == 0) {
      return EMPTY_KEY_VALUE_PAIR;
    }
    buffer = buffer.order(order);
    ArrayList<KeyValuePair> result = new ArrayList();
    try {
      buffer.markReaderIndex();
      while (buffer.readableBytes() > 0) {
        String key = deserializeString(buffer);
        String value = deserializeString(buffer);
        result.add(new KeyValuePair(key, value));
      }
    } finally {
      buffer.resetReaderIndex();
    }
    return result.toArray(new KeyValuePair[result.size()]);
  }

  static ByteBuf serialize(KeyValuePair[] headers, ByteOrder order)
  {
    ArrayList<ByteBuf> buffers = new ArrayList();
    ByteBuf buffer = null;

    for (KeyValuePair pair : headers) {
      buffer = serializeString(pair.key(), buffers, buffer, order);
      buffer = serializeString(pair.value(), buffers, buffer, order);
    }
    if (buffers.isEmpty()) {
      return null;
    } else {
      return Unpooled.wrappedBuffer(buffers.size(), buffers.toArray(new ByteBuf[buffers.size()]));
    }
  }
}
