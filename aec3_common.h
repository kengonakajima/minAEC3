// AEC3で共通利用する定数群。元のWebRTC実装から必要最小限を抽出。
inline constexpr int kNumBlocksPerSecond = 250; // 16 kHz, 64サンプルブロックで1秒あたりのブロック数
inline constexpr size_t kFftLengthBy2 = 64; // FFTで扱う片側サイズ（周波数ビン数）
inline constexpr size_t kFftLengthBy2Plus1 = kFftLengthBy2 + 1; // DC〜Nyquistまでのビン数（実数FFT用）
inline constexpr size_t kFftLengthBy2Minus1 = kFftLengthBy2 - 1; // Nyquistを除いた高周波数側のビン数
inline constexpr size_t kFftLength = 2 * kFftLengthBy2; // 実際にIFFTするサンプル長（128）
inline constexpr size_t kFftLengthBy2Log2 = 6; // kFftLengthBy2 (=64) のlog2値


inline constexpr size_t kBlockSize = kFftLengthBy2; // 1処理ブロックのサンプル数（64）
inline constexpr size_t kBlockSizeLog2 = kFftLengthBy2Log2; // ブロックサイズのlog2値

inline constexpr size_t kMatchedFilterWindowSizeSubBlocks = 32; // 遅延推定で用いる窓幅（サブブロック数）
inline constexpr size_t kMatchedFilterAlignmentShiftSizeSubBlocks =
    kMatchedFilterWindowSizeSubBlocks * 3 / 4;  // 遅延探索時のシフト間隔（サブブロック数）

// ダウンサンプリング後のレンダーバッファサイズを求める。
inline size_t GetDownSampledBufferSize(size_t down_sampling_factor, size_t num_matched_filters) {
  return kBlockSize / down_sampling_factor *
         (kMatchedFilterAlignmentShiftSizeSubBlocks * num_matched_filters +
          kMatchedFilterWindowSizeSubBlocks + 1);
}

// レンダー遅延バッファの必要長を算出する。
inline size_t GetRenderDelayBufferSize(size_t down_sampling_factor,
                                       size_t num_matched_filters,
                                       size_t filter_length_blocks) {
  return GetDownSampledBufferSize(down_sampling_factor, num_matched_filters) /
             (kBlockSize / down_sampling_factor) +
         filter_length_blocks + 1;
}
