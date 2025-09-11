#ifndef MODULES_AUDIO_PROCESSING_AEC3_DOWNSAMPLED_RENDER_BUFFER_H_
#define MODULES_AUDIO_PROCESSING_AEC3_DOWNSAMPLED_RENDER_BUFFER_H_

#include <stddef.h>

#include <vector>
#include "ring_index.h"

 

 

// Holds the circular buffer of the downsampled render data.
struct DownsampledRenderBuffer {
  explicit DownsampledRenderBuffer(size_t downsampled_buffer_size)
      : size(static_cast<int>(downsampled_buffer_size)),
        buffer(downsampled_buffer_size, 0.f) {
    std::fill(buffer.begin(), buffer.end(), 0.f);
  }
  ~DownsampledRenderBuffer() = default;

  int IncIndex(int index) const { return ::IncIndex(index, size); }

  int DecIndex(int index) const { return ::DecIndex(index, size); }

  int OffsetIndex(int index, int offset) const {
    return ::OffsetIndex(index, offset, size);
  }

  void UpdateWriteIndex(int offset) { write = OffsetIndex(write, offset); }
  void IncWriteIndex() { write = IncIndex(write); }
  void DecWriteIndex() { write = DecIndex(write); }
  void UpdateReadIndex(int offset) { read = OffsetIndex(read, offset); }
  void IncReadIndex() { read = IncIndex(read); }
  void DecReadIndex() { read = DecIndex(read); }

  const int size;
  std::vector<float> buffer;
  int write = 0;
  int read = 0;
};

 

#endif  // MODULES_AUDIO_PROCESSING_AEC3_DOWNSAMPLED_RENDER_BUFFER_H_
