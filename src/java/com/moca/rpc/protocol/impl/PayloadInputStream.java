package com.moca.rpc.protocol.impl;

import io.netty.buffer.*;
import java.io.*;
import org.slf4j.*;

public class PayloadInputStream extends InputStream
{
  private static Logger logger = LoggerFactory.getLogger(PayloadInputStream.class);
  private ByteBuf input;
  private int size;

  PayloadInputStream(ByteBuf input)
  {
    this.input = input;
  }

  public int available() throws IOException
  {
    return input.readableBytes();
  }

  public int read() throws IOException
  {
    return input.readByte();
  }

  public int read(byte[] b, int off, int len) throws IOException
  {
    int available = available();
    if (len > available) {
      len = available;
    }

    input.readBytes(b, off, len);
    return len;
  }

  public long skip(long n) throws IOException
  {
    int available;
    long left = n;
    while ((available = available()) > 0 && left > 0) {
      int skip = n > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) n;
      if (skip > available) {
        skip = available;
      }
      input.skipBytes(skip);
      left -= skip;
    }
    return n - left;
  }

  public void close()
  {
    int readable = input.readableBytes();
    if (readable > 0) {
      input.skipBytes(readable);
      logger.warn("There are " + readable + " bytes payload not consumed, discard it");
    }
  }
}
