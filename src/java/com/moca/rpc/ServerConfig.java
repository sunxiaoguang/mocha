package com.moca.rpc;

import com.moca.core.config.*;

public interface ServerConfig
{
  @DefaultConfig("localhost:1234")
  String address();
  @DefaultConfig("localhost:1235")
  String managementAddress();
  @DefaultConfig("2147483647")
  int payloadLimit();

  @DefaultConfig("256")
  int connectionLimit();
  @DefaultConfig("60")
  int connectRateLimit();
  @DefaultConfig("300")
  int statisticExpire();
}
