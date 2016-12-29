package com.moca.rpc.protocol;

import java.net.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

public interface ChannelNano extends Channel
{
  static int LOOP_FLAG_ONE_SHOT = 1;
  static int LOOP_FLAG_ONE_NONBLOCK = 2;

  void loop();
  void loop(int flags);
  public void breakLoop();
  public void keepAlive();
}
