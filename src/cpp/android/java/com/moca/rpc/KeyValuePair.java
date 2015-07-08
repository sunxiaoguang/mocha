package com.moca.rpc;

public final class KeyValuePair
{
  private String key;
  private String value;

  public KeyValuePair(String k, String v)
  {
    this.key = k;
    this.value = v;
  }

  public String getKey()
  {
    return key;
  }

  public String key()
  {
    return key;
  }

  public String getValue()
  {
    return value;
  }

  public String value()
  {
    return value;
  }

  public static KeyValuePair[] create(String ... items)
  {
    if (items.length % 2 != 0) {
      throw new IllegalArgumentException("Arguments must be paired");
    }
    int size = size = items.length / 2;
    KeyValuePair[] pairs = new KeyValuePair[size];
    for (int idx = 0; idx < size; ++idx) {
      int offset = 2 * idx;
      pairs[idx] = new KeyValuePair(items[offset], items[offset + 1]);
    }
    return pairs;
  }

  public String toString()
  {
    return "(" + key + " => " + value + ")";
  }
}
