package com.moca.rpc.protocol;

import java.util.concurrent.atomic.*;
import java.util.*;
import java.io.*;

import org.slf4j.*;

import com.moca.rpc.protocol.impl.*;

public class TestServer implements ChannelListener
{
  private static Logger logger = LoggerFactory.getLogger(TestServer.class);
  private AtomicLong counter = new AtomicLong();
  private volatile long lastTimestamp;
  public void onConnected(Channel channel)
  {
    logger.info("Client " + channel.getRemoteAddress() + " is connected to local server at " + channel.getLocalAddress());
  }

  public void onEstablished(Channel channel)
  {
    logger.info("Session from client " + channel.getRemoteAddress() + "@" + channel.getRemoteId() + " is connected to local server at " + channel.getLocalAddress());
  }

  public void onDisconnected(Channel channel)
  {
    logger.info("Client " + channel.getRemoteAddress() + " is disconnected from local server at " + channel.getLocalAddress());
  }

  public void onRequest(Channel channel, long id, int code, Map<String, String> headers, int payloadSize)
  {
    logger.info("Server received request : " + id + " code " + code + " " + headers + " " + payloadSize);
    /*
    for (int idx = 0; idx < 10000; ++idx) {
      headers.put("id", Integer.toString(idx));
      channel.request(code, headers);
    }
    */
  }

  public void onResponse(Channel channel, long id, int code, Map<String, String> headers, int payloadSize)
  {
    logger.info("Server received response : " + id + " code " + code + " " + headers + " " + payloadSize);
  }

  public void onPayload(Channel channel, long id, int code, InputStream payload, boolean commit)
  {
    logger.info("Server received payload : " + id + " code " + code + " " + commit);
  }

  public void onError(Channel channel, Throwable error)
  {
  }

  public void run(String address) throws Exception
  {
    ChannelImpl channel = (ChannelImpl) (Channel.builder().bind(address).listener(this).build());
  }

  public static void main(String args[]) throws Exception
  {
    TestServer server = new TestServer();
    server.run(args[0]);
  }
}
