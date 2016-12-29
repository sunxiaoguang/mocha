package com.moca.rpc.protocol.impl;

import java.util.*;
import java.util.concurrent.*;

public class CompletedFuture implements Future
{
  private Object result;
  private CompletedFuture()
  {
  }

  public CompletedFuture(Object result)
  {
    this.result = result;
  }

  private static CompletedFuture COMPLETED_FUTURE = new CompletedFuture();

  public static CompletedFuture instance()
  {
    return COMPLETED_FUTURE;
  }

  public boolean cancel(boolean mayInterruptIfRunning)
  {
    return false;
  }

  public boolean isCancelled()
  {
    return false;
  }

  public boolean isDone()
  {
    return true;
  }

  public Object get(long timeout, TimeUnit unit)
  {
    return result;
  }

  public Object get()
  {
    return result;
  }
}
