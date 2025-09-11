#ifndef MODULES_AUDIO_PROCESSING_AEC3_ECHO_PATH_VARIABILITY_H_
#define MODULES_AUDIO_PROCESSING_AEC3_ECHO_PATH_VARIABILITY_H_

 

struct EchoPathVariability {
  enum DelayChange { kNone, kBufferFlush, kNewDetectedDelay };

  explicit EchoPathVariability(DelayChange dc) : delay_change(dc) {}

  bool DelayChanged() const { return delay_change != kNone; }

  DelayChange delay_change;
};

 

#endif  // MODULES_AUDIO_PROCESSING_AEC3_ECHO_PATH_VARIABILITY_H_
