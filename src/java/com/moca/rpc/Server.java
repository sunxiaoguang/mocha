package com.moca.rpc;

import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import java.net.*;

import org.slf4j.*;

import com.moca.rpc.protocol.*;
import com.moca.core.lifecycle.*;
import static com.moca.core.exception.Suppressor.*;

public abstract class Server implements Lifecycle
{
  private static final int CONNECT_WINDOW_SIZE = 15;
  private static final long MINUTE = 60L * 1000L;
  private static final long INSTANCE_EPOCH = System.currentTimeMillis() - (MINUTE * 60L * 24L * 31);

  public static final int RESPONSE_CODE_OK = 200;
  public static final int RESPONSE_CODE_TEMPORARY_REDIRECT = 307;
  public static final int RESPONSE_CODE_BAD_REQUEST = 400;
  public static final int RESPONSE_CODE_UNAUTHORIZED = 401;
  public static final int RESPONSE_CODE_FORBIDDEN = 403;
  public static final int RESPONSE_CODE_NOT_FOUND = 404;
  public static final int RESPONSE_CODE_TIMEOUT = 408;
  public static final int RESPONSE_CODE_UPGRADE_REQUIRED = 426;
  public static final int RESPONSE_CODE_TOO_MANY_REQUEST = 429;
  public static final int RESPONSE_CODE_INTERNAL_ERROR = 500;

  private static Logger logger = LoggerFactory.getLogger(Server.class);
  private ServerConfig config;

  protected Channel getServerChannel()
  {
    return channel;
  }

  protected Channel getManagementChannel()
  {
    return managementChannel;
  }

  private Channel channel;
  private Channel managementChannel;
  private volatile boolean running = true;
  private int connectionLimit;
  private int connectRateLimit;
  private int statisticExpire;
  private int payloadLimit;
  private ConcurrentHashMap<InetAddress, InetAddressContext> addressContext = new ConcurrentHashMap();
  private Thread evictThread;

  private class ServerListener implements ChannelListener
  {
    public void onRequest(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize)
    {
      onServerRequest(channel, id, code, headers, payloadSize);
    }

    public void onResponse(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize)
    {
      if (payloadSize > payloadLimit) {
        logger.error("Payload " + payloadSize + " is larger than limit of " + payloadLimit + ", shutdown channel");
        channel.response(id, RESPONSE_CODE_FORBIDDEN);
        channel.shutdown();
        return;
      }
      onServerResponse(channel, id, code, headers, payloadSize);
    }

    public void onPayload(Channel channel, long id, int code, InputStream payload, boolean commit)
    {
      onServerPayload(channel, id, code, payload, commit);
    }

    public void onConnected(Channel channel)
    {
      InetSocketAddress socketAddress = channel.getRemoteAddress();
      ThreadLocalContext tls = threadLocalContext.get();
      try {
        InetAddress address = socketAddress.getAddress();
        InetAddressContext ctx = tls.borrowAddressContext();
        InetAddressContext old = addressContext.putIfAbsent(address, ctx);
        if (old == null) {
          tls.takeAddressContext();
          old = ctx;
        }
        if (!old.connect(connectionLimit, connectRateLimit, address)) {
          channel.shutdown();
        } else {
          onServerConnected(channel);
        }
      } finally {
        logger.info("Channel " + channel + " is connected");
      }
    }

    public void onEstablished(Channel channel)
    {
      try {
        onServerEstablished(channel);
      } finally {
        logger.info("Session from channel " + channel + " is established");
      }
    }

    public void onDisconnected(Channel channel)
    {
      try {
        onServerDisconnected(channel);
      } finally {
        InetSocketAddress socketAddress = channel.getRemoteAddress();
        ThreadLocalContext tls = threadLocalContext.get();
        InetAddress address = socketAddress.getAddress();
        InetAddressContext ctx = addressContext.get(address);
        if (ctx != null) {
          ctx.disconnect(address);
        }
        logger.info("Channel " + channel + " is disconnected");
      }
    }

    public void onError(Channel channel, Throwable error)
    {
      try {
        onServerError(channel, error);
      } finally {
        logger.error("Caught exception on channel " + channel, error);
      }
    }
  }

  private class ManagementListener implements ChannelListener
  {
    public void onRequest(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize)
    {
      onManagementRequest(channel, id, code, headers, payloadSize);
    }

    public void onResponse(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize)
    {
      onManagementResponse(channel, id, code, headers, payloadSize);
    }

    public void onPayload(Channel channel, long id, int code, InputStream payload, boolean commit)
    {
      onManagementPayload(channel, id, code, payload, commit);
    }

    public void onConnected(Channel channel)
    {
      try {
        onManagementConnected(channel);
      } finally {
        logger.info("Client " + channel.getRemoteAddress() + " is connected to local server at " + channel.getLocalAddress());
      }
    }

    public void onEstablished(Channel channel)
    {
      try {
        onManagementEstablished(channel);
      } finally {
        logger.info("Session from channel " + channel + " is established");
      }
    }

    public void onDisconnected(Channel channel)
    {
      try {
        onManagementDisconnected(channel);
      } finally {
        logger.info("Client " + channel.getRemoteAddress() + " is disconnected from local server at " + channel.getLocalAddress());
      }
    }

    public void onError(Channel channel, Throwable error)
    {
      try {
        onManagementError(channel, error);
      } finally {
        logger.error("Caught exception on channel " + channel, error);
      }
    }
  }

  private static int currentMinute()
  {
    return (int) ((System.currentTimeMillis() - INSTANCE_EPOCH) / MINUTE);
  }

  private void initChannel()
  {
    channel = Channel.builder().bind(config.address()).listener(new ServerListener())
      .timeout(config.idleTimeout(), TimeUnit.SECONDS).limit(config.headerLimit()).build();
    managementChannel = Channel.builder().bind(config.managementAddress()).listener(new ManagementListener()).build();
  }

  private void shutdownChannel()
  {
    safeRun(channel, c -> c.shutdown().get());
    safeRun(managementChannel, c -> c.shutdown().get());
  }

  protected Server(ServerConfig config)
  {
    this.config = config;
    this.connectionLimit = config.connectionLimit();
    if (this.connectionLimit < 1) {
      logger.info("Current connection limit setup per address " + connectionLimit + " is too small, change it to 1");
      this.connectionLimit = 1;
    }
    this.connectRateLimit = config.connectRateLimit();
    if (this.connectRateLimit < 1) {
      logger.info("Current connect rate limit setup " + connectRateLimit + " is too small, change it to 1");
      this.connectRateLimit = 1;
    }
    this.statisticExpire = config.statisticExpire() / 60;
    if (this.statisticExpire < 5) {
      logger.info("Current statistic expiry setup " + config.statisticExpire() + " secs is too small, change it to 5000 secs");
      this.statisticExpire = 5;
    }
    this.payloadLimit = config.payloadLimit();
    if (this.payloadLimit < 4096) {
      logger.info("Current payload limit " + payloadLimit + " is too small, change it to 4096");
      this.payloadLimit = 4096;
    }
  }

  private void initEvictThread()
  {
    evictThread = new Thread(() -> {
      while (running) {
        try {
          doEvict();
          Thread.sleep(30000);
        } catch (InterruptedException ignored) {
        } catch (Error error) {
          logger.error("Caught exception when evicting expired statistic data");
        }
      }
    });
    evictThread.start();
  }

  private static class InetAddressContext
  {
    volatile int minute;
    volatile int connections;
    volatile boolean busted;
    AtomicIntegerArray connects;

    private InetAddressContext()
    {
      connects = new AtomicIntegerArray(CONNECT_WINDOW_SIZE);
    }

    public void init()
    {
      minute = currentMinute();
    }

    public boolean connect(int connectionLimit, int connectRateLimit, InetAddress address)
    {
      if (connections >= connectionLimit || busted) {
        logger.info("Connection from address " + address + " is rejected as number of connections from this address is " + connections + " or it has been busted before next decay");
        return false;
      }
      for (int idx = 0; idx < CONNECT_WINDOW_SIZE; ++idx) {
        if ((connects.get(idx) / 60) > connectRateLimit) {
          busted = true;
          return false;
        }
      }
      int now = currentMinute();
      int index = Math.abs(now % CONNECT_WINDOW_SIZE);
      connectionsUpdater.getAndIncrement(this);
      while (true) {
        int prev = minute;
        if (minuteUpdater.compareAndSet(this, prev, now)) {
          connects.set((index + 1) % CONNECT_WINDOW_SIZE, 0);
          busted = false;
          break;
        }
      }
      connects.getAndIncrement(index);
      logger.info("New connection from address " + address + " is accepted, new number of connections from this address is " + connections);
      return true;
    }

    public void disconnect(InetAddress address)
    {
      connectionsUpdater.getAndDecrement(this);
      logger.info("Connection from address " + address + " is closed, new number of connections from this address is " + connections);
    }
  }

  private static AtomicIntegerFieldUpdater connectionsUpdater = AtomicIntegerFieldUpdater.newUpdater(InetAddressContext.class, "connections");
  private static AtomicIntegerFieldUpdater minuteUpdater = AtomicIntegerFieldUpdater.newUpdater(InetAddressContext.class, "minute");

  private static class ThreadLocalContext
  {
    InetAddressContext addressContext = new InetAddressContext();

    public InetAddressContext borrowAddressContext()
    {
      if (addressContext == null) {
        addressContext = new InetAddressContext();
      }
      addressContext.init();
      return addressContext;
    }

    public void takeAddressContext()
    {
      this.addressContext = null;
    }
  }

  private static ThreadLocal<ThreadLocalContext> threadLocalContext = new ThreadLocal<ThreadLocalContext>()
  {
    @Override
    public ThreadLocalContext initialValue()
    {
      return new ThreadLocalContext();
    }
  };

  private void doEvict()
  {
    int now = currentMinute();
    for (Map.Entry<InetAddress, InetAddressContext> entry : addressContext.entrySet()) {
      InetAddressContext value = entry.getValue();
      if (now - value.minute > statisticExpire) {
        InetAddress address = entry.getKey();
        boolean evicted = addressContext.remove(address, value);
        if (evicted) {
          logger.info("Statistic data for clients from address " + address + " is removed because it's been idle for " + statisticExpire + " minutes");
        }
      }
    }
  }

  public abstract void onManagementConnected(Channel channel);
  public abstract void onManagementEstablished(Channel channel);
  public abstract void onManagementDisconnected(Channel channel);
  public abstract void onServerConnected(Channel channel);
  public abstract void onServerEstablished(Channel channel);
  public abstract void onServerDisconnected(Channel channel);

  public abstract void onManagementRequest(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize);
  public abstract void onManagementResponse(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize);
  public abstract void onServerRequest(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize);
  public abstract void onServerResponse(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize);
  public abstract void onManagementPayload(Channel channel, long id, int code, InputStream payload, boolean commit);
  public abstract void onServerPayload(Channel channel, long id, int code, InputStream payload, boolean commit);
  public abstract void onManagementError(Channel channel, Throwable error);
  public abstract void onServerError(Channel channel, Throwable error);

  public void init()
  {
  }

  public void start()
  {
    initChannel();
    initEvictThread();
    logger.info(getClass().getSimpleName() + " server is start and listening on " + config.address());
    logger.info(getClass().getSimpleName() + " management extension is start and listening on " + config.managementAddress());
  }

  public void stop()
  {
    safeRun(evictThread, t -> {running = false; t.interrupt(); t.join();});
    shutdownChannel();
    logger.info(getClass().getSimpleName() + " is fully stopped");
  }

  public void destroy()
  {
    logger.info(getClass().getSimpleName() + " is destroyed");
  }
}
