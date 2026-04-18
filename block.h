#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include "aec3_common.h"

// 4ミリ秒分のモノラル音声データ（64サンプル=4ms、16kHzモノラル）。
using Block = std::array<float, kBlockSize>;

// 16bit PCMからブロックへコピーする。
inline void CopyFromPcm16(const int16_t* src, Block* dst) {
  for (size_t i = 0; i < dst->size(); ++i) {
    (*dst)[i] = static_cast<float>(src[i]);
  }
}

// ブロック内容を16bit PCMへ書き出す。
inline void CopyToPcm16(const Block& src, int16_t* dst) {
  for (size_t i = 0; i < src.size(); ++i) {
    float v = src[i];
    v = std::min(v, 32767.f);
    v = std::max(v, -32768.f);
    dst[i] = static_cast<int16_t>(v + std::copysign(0.5f, v));
  }
}
