package com.moca.rpc.protocol.impl;

import java.util.concurrent.*;
import java.net.*;

import org.slf4j.*;

import com.moca.rpc.protocol.*;

import io.netty.bootstrap.*;
import io.netty.channel.*;
import io.netty.channel.socket.*;
import io.netty.channel.socket.nio.*;
import io.netty.handler.timeout.*;

public class ServerChannel extends ChannelImpl
{
  private EventLoopGroup parentGroup;
  private EventLoopGroup childGroup;
  private io.netty.channel.Channel listenChannel;
  private static Logger logger = LoggerFactory.getLogger(ServerChannel.class);

  public ServerChannel(final ChannelListener listener, InetSocketAddress address, int timeout, int limit)
  {
    boolean cleanup = true;
    try {
      parentGroup = createListenEventLoopGroup();
      childGroup = createSocketEventLoopGroup();
      ServerBootstrap bootstrap = new ServerBootstrap();
      bootstrap.group(parentGroup, childGroup)
        .channel(NioServerSocketChannel.class)
        .localAddress(address)
        .childHandler(new ChannelInitializer<SocketChannel>() {
          @Override
          protected void initChannel(SocketChannel ch)
          {
            final ChannelImpl newChannel = new ChannelImpl();
            ChannelPipeline pipeline = ch.pipeline();
            pipeline.addLast(new RPCHandler(newChannel, listener, limit));
            pipeline.addLast(new IdleStateHandler(timeout, timeout, timeout) {
              @Override
              protected void channelIdle(ChannelHandlerContext ctx, IdleStateEvent evt)
              {
                logger.warn("Channel " + newChannel + " has been idle for " + timeout + " sec(s), shut it down");
                ctx.close();
              }
            });
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
        listenChannel != null ? listenChannel.close() : RPCFuture.completed(),
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
