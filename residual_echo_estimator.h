
struct ResidualEchoEstimator {
  inline static constexpr size_t kRenderPreWindowSize = 1; // 遅延推定前側で参照するスペクトル数
  inline static constexpr size_t kRenderPostWindowSize = 1; // 遅延推定後側で参照するスペクトル数


  ResidualEchoEstimator() = default;

  static void GetRenderIndexesToAnalyze(const SpectrumBuffer& spectrum_buffer,
                                        int filter_delay_blocks,
                                        int* idx_start,
                                        int* idx_stop) {
    const size_t window_start =
        std::max(0, filter_delay_blocks - static_cast<int>(kRenderPreWindowSize));
    const size_t window_end =
        filter_delay_blocks + static_cast<int>(kRenderPostWindowSize);
    *idx_start = spectrum_buffer.OffsetIndex(spectrum_buffer.read, window_start);
    *idx_stop =
        spectrum_buffer.OffsetIndex(spectrum_buffer.read, window_end + 1);
  }

  static void ComputeEchoGeneratingPower(const SpectrumBuffer& spectrum_buffer,
                                         int filter_delay_blocks,
                                         std::span<float, kFftLengthBy2Plus1> X2) {
    int idx_stop;
    int idx_start;
    GetRenderIndexesToAnalyze(spectrum_buffer, filter_delay_blocks,
                              &idx_start, &idx_stop);
    std::fill(X2.begin(), X2.end(), 0.f);
    for (int index = idx_start; index != idx_stop; index = spectrum_buffer.IncIndex(index)) {
      for (size_t bin = 0; bin < kFftLengthBy2Plus1; ++bin) {
        X2[bin] = std::max(X2[bin], spectrum_buffer.buffer[index][bin]);
      }
    }
  }


  // 残留エコーのパワースペクトルR2を推定する。
  void Estimate(const AecState& aec_state,
                const RenderBuffer& render_buffer,
                const std::array<float, kFftLengthBy2Plus1>& S2_linear,
                const std::array<float, kFftLengthBy2Plus1>& Y2,
                std::array<float, kFftLengthBy2Plus1>* R2) {
    if (aec_state.UsableLinearEstimate()) {
      // Linear mode: R2 = S2_linear / ERLE
      const std::array<float, kFftLengthBy2Plus1>& erle = aec_state.Erle();
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        (*R2)[k] = S2_linear[k] / erle[k];
      }
    } else {
      // 非線形モード: R2 ≈ 遠端スペクトル(エコー生成パワー)
      std::array<float, kFftLengthBy2Plus1> X2;
      ComputeEchoGeneratingPower(render_buffer.GetSpectrumBuffer(),
                                  aec_state.MinDirectPathFilterDelay(), X2);
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        (*R2)[k] = X2[k];
      }
    }
  }

};



