package com.mocha.rpc;

import java.io.*;
import java.util.*;
import com.mocha.rpc.protocol.*;

public class TestChannelNano
{
  private static volatile boolean running = true;
  public static void main(String[] args) throws Exception
  {
    ChannelNano channel = new ChannelBuilder().connect(args[0]).listener(new ChannelListener() {
      public void onRequest(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize)
      {
        System.out.println("Request " + id + " with code " + code + " from server " + channel.getRemoteId() + "@" +
          channel.getRemoteAddress() + " with " + payloadSize + " bytes bytes");
        System.out.println(Arrays.toString(headers));
      }
      public void onResponse(Channel channel, long id, int code, KeyValuePair[] headers, int payloadSize)
      {
        System.out.println("Response " + id + " with code " + code + " from server " + channel.getRemoteId() + "@" +
          channel.getRemoteAddress() + " with " + payloadSize + " bytes bytes");
        System.out.println(Arrays.toString(headers));
      }
      public void onPayload(Channel channel, long id, int code, InputStream payload, boolean commit)
      {
        try {
          System.out.println("Payload of packet " + id + " from server " + channel.getRemoteId() + "@" +
            channel.getRemoteAddress() + " is " + payload.available() + " bytes, commit = " + commit);
        } catch (Exception ex) {
          ex.printStackTrace();
        }
      }
      public void onConnected(Channel channel)
      {
        System.out.println("Connected to server " + channel.getRemoteAddress() + " from " + channel.getLocalAddress());
      }
      public void onEstablished(Channel channel)
      {
        System.out.println("Session to server " + channel.getRemoteId() + "@" + channel.getRemoteAddress() + " from " +
          channel.getLocalId() + "@" + channel.getLocalAddress() + " is established");
        channel.request(2, KeyValuePair.create("s", "t.0", "s", "t.1", "s", "t.010", "s", "cc",
                                               "s", "w.0", "s", "w.1", "s", "w.010",
                                               "s", "c.0", "s", "c.0", "s", "c.0"));
      }
      public void onDisconnected(Channel channel)
      {
        running = false;
        ((ChannelNano) channel).breakLoop();
        System.out.println("Connection to server " + channel.getRemoteAddress() + " from " + channel.getLocalAddress() + " is disconnected");
      }
      public void onError(Channel channel, Throwable error)
      {
        System.out.println("Caught exception on Connection to server " + channel.getRemoteAddress() + " from " + channel.getLocalAddress());
        error.printStackTrace();
      }
    }).nano();

    Thread mainThread = Thread.currentThread();
    Runtime.getRuntime().addShutdownHook(new Thread() {
      public void run() {
        running = false;
        if (channel != null) {
          channel.breakLoop();
        }
        try {
          mainThread.join();
        } catch (Exception ignored) {
        }
      }
    });

    System.out.println("Start ChannelNano Event Loop");
    while (running) {
      try {
        channel.loop();
      } catch (Exception ex) {
        ex.printStackTrace();
      }
    }
    System.out.println("Returned from ChannelNano Event Loop, close it");
    channel.shutdown();
    System.out.println("ChannelNano is closed and may not be used anymore");
  }
}
