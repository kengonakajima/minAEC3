#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include "aec3_common.h"

// 4ミリ秒分のモノラル音声データを保持する。
// 単一バンドでサンプリング周波数は16 kHz固定。
struct Block {
  std::array<float, kBlockSize> data_; // 固定長サンプル列（64サンプル=4ms）
    
  Block() : data_{} {
    data_.fill(0.0f);
  }

  // データへアクセスするためのイテレータ。
  std::array<float, kBlockSize>::iterator begin() { return data_.begin(); }
  std::array<float, kBlockSize>::const_iterator begin() const { return data_.begin(); }
  std::array<float, kBlockSize>::iterator end() { return data_.end(); }
  std::array<float, kBlockSize>::const_iterator end() const { return data_.end(); }

  // std::span経由でデータ全体を参照する。
  std::span<float, kBlockSize> View() {
    return std::span<float, kBlockSize>(data_.data(), kBlockSize);
  }

  std::span<const float, kBlockSize> View() const {
    return std::span<const float, kBlockSize>(data_.data(), kBlockSize);
  }

  // 16bit PCMからブロックへコピーする。
  void CopyFromPcm16(const int16_t* src) {
    std::span<float, kBlockSize> dst = View();
    for (size_t i = 0; i < dst.size(); ++i) {
      dst[i] = static_cast<float>(src[i]);
    }
  }

  // ブロック内容を16bit PCMへ書き出す。
  void CopyToPcm16(int16_t* dst) const {
    std::span<const float, kBlockSize> src = View();
    for (size_t i = 0; i < src.size(); ++i) {
      float v = src[i];
      v = std::min(v, 32767.f);
      v = std::max(v, -32768.f);
      dst[i] = static_cast<int16_t>(v + std::copysign(0.5f, v));
    }
  }
};

 
