package com.mocha.rpc.protocol.impl;

import java.util.concurrent.*;
import java.net.*;

import org.slf4j.*;

import com.mocha.rpc.protocol.*;

import io.netty.bootstrap.*;
import io.netty.channel.*;
import io.netty.channel.socket.*;
import io.netty.channel.socket.nio.*;
import io.netty.handler.timeout.*;
import io.netty.handler.ssl.*;
import io.netty.handler.logging.*;

public class ServerChannel extends JavaChannel
{
  private EventLoopGroup parentGroup;
  private EventLoopGroup childGroup;
  private io.netty.channel.Channel listenChannel;
  private static Logger logger = LoggerFactory.getLogger(ServerChannel.class);

  public ServerChannel(String id, final ChannelListener listener, InetSocketAddress address, int timeout, int keepaliveInterval, int limit, boolean debug, Object ssl)
  {
    super(id, true);
    boolean cleanup = true;
    try {
      parentGroup = createListenEventLoopGroup();
      childGroup = createSocketEventLoopGroup();
      ServerBootstrap bootstrap = new ServerBootstrap().group(parentGroup, childGroup).channel(NioServerSocketChannel.class);
      if (debug) {
        bootstrap.handler(new LoggingHandler());
      }
      bootstrap.localAddress(address)
        .childHandler(new ChannelInitializer<SocketChannel>() {
          @Override
          protected void initChannel(SocketChannel ch)
          {
            final JavaChannel newChannel = new JavaChannel(id, false);
            ChannelPipeline pipeline = ch.pipeline();
            final int finalTimeout = (timeout <= 0 ? Integer.MAX_VALUE : timeout) * 1000;
            final int finalKeepaliveInterval = (keepaliveInterval <= 0 ? Integer.MAX_VALUE : keepaliveInterval) * 1000;
            if (finalTimeout < Integer.MAX_VALUE || finalKeepaliveInterval < Integer.MAX_VALUE) {
              int interval = Math.min(finalTimeout, finalKeepaliveInterval) / 1000;
              pipeline.addLast("IdleHandler", new IdleStateHandler(interval, interval, interval) {
                @Override
                protected void channelIdle(ChannelHandlerContext ctx, IdleStateEvent evt)
                {
                  if (getWriterIdleTimeInMillis() >= finalKeepaliveInterval) {
                    newChannel.keepalive(finalKeepaliveInterval);
                  }
                  if (getAllIdleTimeInMillis() >= finalTimeout) {
                    logger.warn("Channel " + newChannel + " has been idle for " + timeout + " sec(s), shut it down");
                    ctx.close();
                  }
                }
              });
            }
            if (ssl != null) {
              pipeline.addLast(((SslContext) ssl).newHandler(ch.alloc()));
            }
            pipeline.addLast("Handler", new RPCHandler(newChannel, listener, limit, true));
          }
        })
        .childOption(ChannelOption.SO_KEEPALIVE, true)
        .childOption(ChannelOption.TCP_NODELAY, true);
      ChannelFuture future = bootstrap.bind().sync();
      listenChannel = future.channel();
      cleanup = false;
    } catch (InterruptedException ex) {
      throw new RuntimeException(ex);
    } finally {
      if (cleanup) {
        shutdown();
      }
    }
  }

  public Future shutdown()
  {
    return new RPCFuture(new Future[] {
        super.shutdown(),
        listenChannel != null ? listenChannel.close() : CompletedFuture.instance(),
        stopListenEventLoopGroup(parentGroup),
        stopSocketEventLoopGroup(childGroup),
    });
  }

  public InetSocketAddress getLocalAddress()
  {
    return (InetSocketAddress) listenChannel.localAddress();
  }

  public InetSocketAddress getRemoteAddress()
  {
    return (InetSocketAddress) listenChannel.remoteAddress();
  }

  public String toString()
  {
    return "{type:server remote:" + getRemoteAddress() + " local:" + getLocalAddress() + "}";
  }
}
