#ifndef MODULES_AUDIO_PROCESSING_AEC3_DECIMATOR_H_
#define MODULES_AUDIO_PROCESSING_AEC3_DECIMATOR_H_

#include <span>

// 係数4での単純なダウンサンプル（平均化）。
// in: 元のフレーム（例: 64サンプル）
// out: ダウンサンプル先（例: 16サンプル）
inline void DecimateBy4(std::span<const float> in, std::span<float> out) {
  const size_t f = 4;
  for (size_t j = 0; j < out.size(); ++j) {
    const size_t k0 = j * f;
    float acc = 0.f;
    size_t cnt = 0;
    for (size_t t = 0; t < f && (k0 + t) < in.size(); ++t) {
      acc += in[k0 + t];
      ++cnt;
    }
    out[j] = cnt ? (acc / static_cast<float>(cnt)) : 0.f;
  }
}

#endif  // MODULES_AUDIO_PROCESSING_AEC3_DECIMATOR_H_
