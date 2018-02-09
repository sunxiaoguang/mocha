package com.mocha.rpc;

import com.mocha.core.config.*;

public interface ServerConfig
{
  @DefaultConfig("localhost:1234")
  String address();
  @DefaultConfig("localhost:1235")
  String managementAddress();
  @DefaultConfig("2147483647")
  int payloadLimit();
  /* idle timeout in seconds */
  @DefaultConfig("300")
  int idleTimeout();
  @DefaultConfig("4194304")
  int headerLimit();

  @DefaultConfig("256")
  int connectionLimit();
  @DefaultConfig("60")
  int connectRateLimit();
  /* expire time of statistic data in seconds */
  @DefaultConfig("300")
  int statisticExpire();
}
