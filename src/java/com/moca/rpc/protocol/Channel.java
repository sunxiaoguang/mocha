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
    Builder id(String id);
    Channel build();
  }

  public static Builder builder()
  {
    return ChannelImpl.builder();
  }

  public abstract Future response(long id, Map<String, String> response);
  public abstract Future request(Map<String, String> request);
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
