package com.mocha.rpc;

import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import com.mocha.rpc.protocol.*;

public class TestChannelEasy
{
  private static volatile boolean running = true;
  public static void main(String[] args) throws Exception
  {
    ChannelEasy channel = new ChannelBuilder().connect(args[0]).easy();
    Future future = channel.request(2, KeyValuePair.create("s", "t.0", "s", "t.1", "s", "t.010", "s", "cc", "s", "w.0", "s", "w.1", "s", "w.010", "s", "c.0", "s", "c.0", "s", "c.0"));
    System.err.println(future.get());
    int loop = 10;
    while (--loop >= 0) {
      System.err.println(channel.poll());
    }
    System.err.println("closing");
    channel.shutdown();
    System.err.println("closed");
  }
}
