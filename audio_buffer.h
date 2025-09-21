// 音声処理モジュールが制御しながら操作できるように音声データを保持する。
struct AudioBuffer {
  std::vector<float> data_; // モノラルの時間領域サンプルデータ

  AudioBuffer() : data_(kBlockSize) {}

  float* mono_data() { return data_.data(); }
  const float* mono_data_const() const { return data_.data(); }

  // 16bit PCMデータを内部バッファへコピーする。
  void CopyFrom(const int16_t* const src_data) {
    const int16_t* src = src_data;
    float* dst = mono_data();
    for (size_t j = 0; j < kBlockSize; ++j) {
      dst[j] = static_cast<float>(src[j]);
    }
  }

  // 内部バッファの内容を16bit PCMとして書き出す。
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
};

 
