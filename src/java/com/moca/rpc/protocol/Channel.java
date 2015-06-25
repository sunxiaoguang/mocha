package com.moca.rpc.protocol;

import java.util.*;
import java.util.concurrent.*;
import java.net.*;
import java.io.*;

import com.moca.rpc.protocol.impl.*;

public abstract class Channel
{
  private volatile Object attachment;

  public interface Builder
  {
    Builder bind(String address);
    Builder connect(String address);
    Builder timeout(long timeout, TimeUnit unit);
    Builder limit(int size);
    Builder listener(ChannelListener listener);
    Builder debug();
    Builder ssl();
    Builder ssl(File certificateChainFile, File privateKey);
    Builder ssl(File certificateChainFile, File privateKey, String keyPassword);
    Builder ssl(File certificateChainFile, File privateKey, Callable<String> keyPasswordProvider);
    Builder keepalive(long interval, TimeUnit unit);
    Builder id(String id);
    Builder dispatchThread();
    Channel build();
  }

  public static Builder builder()
  {
    return ChannelImpl.builder();
  }

  public abstract Future response(long id, int code, Map<String, String> headers, byte[] payload, int offset, int size);
  public Future response(long id, int code, Map<String, String> headers, byte[] payload)
  {
    return response(id, code, headers, payload, 0, payload.length);
  }
  public Future response(long id, int code, Map<String, String> headers)
  {
    return response(id, code, headers, null, 0, 0);
  }
  public Future response(long id, int code)
  {
    return response(id, code, null, null, 0, 0);
  }

  public abstract Future request(int code, Map<String, String> headers, byte[] payload, int offset, int size);
  public Future request(int code, Map<String, String> headers, byte[] payload)
  {
    return request(code, headers, payload, 0, payload.length);
  }
  public Future request(int code, Map<String, String> headers)
  {
    return request(code, headers, null, 0, 0);
  }
  public Future request(int code)
  {
    return request(code, null, null, 0, 0);
  }
  public abstract Future shutdown();

  public abstract InetSocketAddress getLocalAddress();
  public abstract InetSocketAddress getRemoteAddress();

  public abstract String getLocalId();
  public abstract String getRemoteId();

  public String toString()
  {
    return getRemoteAddress() + " => " + getLocalAddress();
  }

  public <T> void setAttachment(T attachment)
  {
    this.attachment = attachment;
  }

  public Object getAttachment()
  {
    return attachment;
  }

  public <T> T getAttachment(Class<T> type)
  {
    return (T) getAttachment();
  }
}
