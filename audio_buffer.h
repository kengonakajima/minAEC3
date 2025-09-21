#include <stddef.h>
#include <stdint.h>

#include <vector>
#include <algorithm>
#include <cmath>


// Stores any audio data in a way that allows the audio processing module to
// operate on it in a controlled manner.
struct AudioBuffer {

  AudioBuffer() : data_(kBlockSize) {}



  float* mono_data() { return data_.data(); }
  const float* mono_data_const() const { return data_.data(); }

  // Copies data into the buffer.
  void CopyFrom(const int16_t* const src_data) {
    const int16_t* src = src_data;
    float* dst = mono_data();
    for (size_t j = 0; j < kBlockSize; ++j) {
      dst[j] = static_cast<float>(src[j]);
    }
  }

  // Copies data from the buffer.
  void CopyTo(int16_t* const dst_data) {
    int16_t* out = dst_data;
    const float* mono_ptr = mono_data_const();
    for (size_t j = 0; j < kBlockSize; ++j) {
      float v = mono_ptr[j];
      v = std::min(v, 32767.f);
      v = std::max(v, -32768.f);
      out[j] = static_cast<int16_t>(v + std::copysign(0.5f, v));
    }
  }
  
  

  // 単一チャネルの実データ
  std::vector<float> data_;
};

 

