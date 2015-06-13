package com.moca.rpc.protocol.impl;

import java.net.*;
import java.util.concurrent.atomic.*;
import java.util.concurrent.*;
import java.nio.*;

import io.netty.buffer.*;
import io.netty.bootstrap.*;
import io.netty.channel.*;
import io.netty.channel.nio.*;
import io.netty.channel.socket.*;
import io.netty.channel.socket.nio.*;
import io.netty.handler.ssl.*;
import io.netty.handler.logging.*;

import com.moca.rpc.protocol.*;

public class ClientChannel extends ChannelImpl
{
  private EventLoopGroup loopGroup;

  public ClientChannel(String id, final ChannelListener listener, InetSocketAddress address, int timeout, int limit, boolean debug, SslContext ssl)
  {
    super(id);
    boolean cleanup = true;
    try {
      loopGroup = createSocketEventLoopGroup();
      Bootstrap bootstrap = new Bootstrap().group(loopGroup)
        .channel(NioSocketChannel.class)
        .remoteAddress(address);
      if (debug) {
        bootstrap.handler(new LoggingHandler());
      }
      bootstrap.handler(new ChannelInitializer<SocketChannel>() {
          @Override
          public void initChannel(SocketChannel ch)
          {
            ChannelPipeline pipeline = ch.pipeline();
            if (ssl != null) {
              pipeline.addLast(ssl.newHandler(ch.alloc()));
            }
            pipeline.addLast(new RPCHandler(ClientChannel.this, listener, limit, true));
          }
        })
        .option(ChannelOption.SO_KEEPALIVE, true)
        .option(ChannelOption.TCP_NODELAY, true);
      bootstrap.connect().sync();
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
      super.shutdown(), stopSocketEventLoopGroup(loopGroup),
    });
  }

  public String toString()
  {
    return "{type:client remote:" + getRemoteAddress() + " local:" + getLocalAddress() + "}";
  }
}
