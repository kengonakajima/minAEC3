// エコーパスの変化を通知する
struct EchoPathVariability {
  enum DelayChange {
      kNone,
      kBufferFlush,
      kNewDetectedDelay
  };

  explicit EchoPathVariability(DelayChange dc) : delay_change(dc) {}

  bool DelayChanged() const { return delay_change != kNone; }

  DelayChange delay_change;
};

