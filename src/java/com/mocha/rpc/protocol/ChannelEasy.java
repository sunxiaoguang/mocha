package com.mocha.rpc.protocol;

import java.net.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

public interface ChannelEasy extends Channel
{
  class Packet
  {
    private long id;
    private int code;
    private KeyValuePair[] headers;
    private byte[] payload;
    private boolean isResponse;

    protected Packet(long id, int code, KeyValuePair[] headers, byte[] payload, boolean isResponse)
    {
      this.id = id;
      this.code = code;
      this.headers = headers;
      this.payload = payload;
      this.isResponse = isResponse;
    }

    public long id()
    {
      return id;
    }
    public int code()
    {
      return code;
    }
    public KeyValuePair[] headers()
    {
      return headers;
    }
    public byte[] payload()
    {
      return payload;
    }
    public boolean isResponse()
    {
      return isResponse;
    }
    public String toString()
    {
      StringBuilder sb = new StringBuilder(isResponse ? "Response:" : "Request:");
      sb.append("id = ");
      sb.append(id);
      sb.append(", code = ");
      sb.append(code);
      sb.append(", headers = ");
      sb.append(Arrays.toString(headers));
      sb.append(", payload = ");
      sb.append(Arrays.toString(payload));
      return sb.toString();
    }
  }

  class Request extends Packet
  {
    protected Request(long id, int code, KeyValuePair[] headers, byte[] payload)
    {
      super(id, code, headers, payload, false);
    }
  }

  class Response extends Packet
  {
    protected Response(long id, int code, KeyValuePair[] headers, byte[] payload)
    {
      super(id, code, headers, payload, true);
    }
  }

  Packet poll();
  void interrupt();
}
