package com.moca.rpc.protocol;

import io.netty.buffer.*;

public class TestBuffer
{
  public static void main(String[] args) {
    ByteBuf input = Unpooled.buffer(4);
    input.writeInt(1);
    ByteBuf queue = Unpooled.buffer(4);
    queue.writeInt(1);

    ByteBuf buffer = Unpooled.wrappedBuffer(input, queue);
    byte[] tmp = new byte[5];
    buffer.readBytes(tmp);

    System.err.println(buffer);
    System.err.println(input);
    System.err.println(queue);
  }
}
