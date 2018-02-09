package com.mocha.rpc.protocol.impl;

import java.util.*;
import java.util.concurrent.*;

public class RPCFuture implements Future<Void>
{
  private static Future[] EMPTY_FUTURES = new Future[0];
  private Future[] futures = EMPTY_FUTURES;
  private RPCFuture()
  {
  }

  public RPCFuture(Future[] futures)
  {
    this.futures = Arrays.copyOf(futures, futures.length);
  }

  public boolean cancel(boolean mayInterruptIfRunning)
  {
    return Arrays.stream(futures).allMatch(f -> f.cancel(mayInterruptIfRunning));
  }

  public boolean isCancelled()
  {
    return Arrays.stream(futures).allMatch(f -> f.isCancelled());
  }

  public boolean isDone()
  {
    return Arrays.stream(futures).allMatch(f -> f.isDone());
  }

  private static final class Timeout
  {
    private long timeout;
    private Timeout(long timeout)
    {
      this.timeout = timeout;
    }

    private void elapsed(long time)
    {
      timeout -= time;
      if (timeout < 0) {
        timeout = 0;
      }
    }

    private long getTimeout()
    {
      return timeout;
    }
  }

  public Void get(long timeout, TimeUnit unit)
  {
    final Timeout to = new Timeout(timeout);
    Arrays.stream(futures).forEach(f -> {
        long start = System.currentTimeMillis();
        try {
          f.get(to.getTimeout(), unit);
        } catch (Exception ex) {
          throw new RuntimeException(ex);
        }
        to.elapsed(unit.convert(System.currentTimeMillis() - start, TimeUnit.MILLISECONDS));
      });
    return null;
  }

  public Void get()
  {
    Arrays.stream(futures).forEach(f -> {
        try {
          f.get();
        } catch (Exception ex) {
          throw new RuntimeException(ex);
        }
      });
    return null;
  }
}
