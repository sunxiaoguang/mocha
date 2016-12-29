package com.moca.rpc.protocol.impl;

import java.util.concurrent.*;

import io.netty.channel.*;

public class EventLoopGroupFactory
{
  private Class<? extends EventLoopGroup> clazz;
  public EventLoopGroupFactory(Class<? extends EventLoopGroup> clazz)
  {
    this.clazz = clazz;
  }

  public EventLoopGroup create()
  {
    try {
      return clazz.newInstance();
    } catch (RuntimeException ex) {
      throw ex;
    } catch (Exception ex) {
      throw new RuntimeException(ex);
    }
  }

  public Future shutdown(EventLoopGroup group)
  {
    if (group != null) {
      return group.shutdownGracefully();
    } else {
      return CompletedFuture.instance();
    }
  }
}
