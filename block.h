#ifndef MODULES_AUDIO_PROCESSING_AEC3_BLOCK_H_
#define MODULES_AUDIO_PROCESSING_AEC3_BLOCK_H_

#include <array>

#include <span>
#include "aec3_common.h"

 

// Contains 4 milliseconds of mono audio data.
// Single-band, sampling rate 16 kHz 固定。
struct Block {
  Block(float default_value = 0.0f)
      : data_{} {
    data_.fill(default_value);
  }

  // 1ch固定、サイズは常にkBlockSize。

  // Iterators for accessing the data.
  auto begin() { return data_.begin(); }

  auto begin() const { return data_.begin(); }

  auto end() { return data_.begin() + kBlockSize; }

  auto end() const { return data_.begin() + kBlockSize; }

  // Access data via std::span.
  std::span<float, kBlockSize> View() {
    return std::span<float, kBlockSize>(data_.data(), kBlockSize);
  }

  std::span<const float, kBlockSize> View() const {
    return std::span<const float, kBlockSize>(data_.data(), kBlockSize);
  }

  // Swapは未使用のため削除。

  std::array<float, kBlockSize> data_;
};

 
#endif  // MODULES_AUDIO_PROCESSING_AEC3_BLOCK_H_
