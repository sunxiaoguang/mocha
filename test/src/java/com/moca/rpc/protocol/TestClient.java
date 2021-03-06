package com.mocha.rpc.protocol;

import java.util.concurrent.atomic.*;
import java.util.*;
import java.io.*;

import org.slf4j.*;

import com.mocha.rpc.protocol.impl.*;

public class TestClient implements ChannelListener
{
  private static Logger logger = LoggerFactory.getLogger(TestClient.class);
  private AtomicLong counter = new AtomicLong();
  private volatile long lastTimestamp;
  public void onConnected(Channel channel)
  {
    logger.info("Client " + channel.getRemoteAddress() + " is connected to local client at " + channel.getLocalAddress());
  }

  public void onEstablished(Channel channel)
  {
    logger.info("Session from client " + channel.getRemoteAddress() + "@" + channel.getRemoteId() + " is connected to local client at " + channel.getLocalAddress());
    channel.request(0, KeyValuePair.create("abc", "def"));
  }

  public void onDisconnected(Channel channel)
  {
    logger.info("Client " + channel.getRemoteAddress() + " is disconnected from local client at " + channel.getLocalAddress());
  }

  public void onRequest(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize)
  {
    logger.info("Client received request : " + id + " code " + code + " " + Arrays.toString(headers) + " " + payloadSize);
  }

  public void onResponse(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize)
  {
    logger.info("Client received response : " + id + " code " + code + " " + Arrays.toString(headers) + " " + payloadSize);
  }

  public void onPayload(Channel channel, long id, int code, InputStream payload, boolean commit)
  {
    logger.info("Client received payload : " + id + " code " + code + " " + commit);
  }

  public void onError(Channel channel, Throwable error)
  {
  }

  public void run(String address) throws Exception
  {
    Channel channel = new ChannelBuilder().connect(address).listener(this).build();
  }

  public static void main(String args[]) throws Exception
  {
    TestClient client = new TestClient();
    client.run(args[0]);
  }
}
