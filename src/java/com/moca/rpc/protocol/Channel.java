package com.moca.rpc.protocol;

import java.util.*;
import java.util.concurrent.*;

import com.moca.rpc.protocol.impl.*;

import java.net.*;

public abstract class Channel
{
  private volatile Object attachment;

  public interface Builder
  {
    Builder bind(String address);
    Builder timeout(long timeout, TimeUnit unit);
    Builder limit(int size);
    Builder listener(ChannelListener listener);
    Channel build();
  }

  public static Builder builder()
  {
    return ChannelImpl.builder();
  }

  public abstract Future response(Map<String, String> response);
  public abstract Future shutdown();

  public abstract InetSocketAddress getLocalAddress();
  public abstract InetSocketAddress getRemoteAddress();

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
