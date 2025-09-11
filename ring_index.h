// Simple helpers for circular buffer indexing.
#ifndef MODULES_AUDIO_PROCESSING_AEC3_RING_INDEX_H_
#define MODULES_AUDIO_PROCESSING_AEC3_RING_INDEX_H_

inline int IncIndex(int index, int size) {
  return index < size - 1 ? index + 1 : 0;
}

inline int DecIndex(int index, int size) {
  return index > 0 ? index - 1 : size - 1;
}

inline int OffsetIndex(int index, int offset, int size) {
  return (size + index + offset) % size;
}

#endif  // MODULES_AUDIO_PROCESSING_AEC3_RING_INDEX_H_

