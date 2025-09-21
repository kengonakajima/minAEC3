
// 自己回帰型の線形フィルタ更新ゲインを計算する。
struct FilterUpdateGain {
  FilterUpdateGain()
      : poor_excitation_counter_(kPoorExcitationCounterInitial) {
    H_error_.fill(kHErrorInitial);
  }
  


  // Takes action in the case of a known echo path change.
  void HandleEchoPathChange(const EchoPathVariability& echo_path_variability) {
    if (echo_path_variability.delay_change != EchoPathVariability::kNone) {
      H_error_.fill(kHErrorInitial);
    }
    poor_excitation_counter_ = kPoorExcitationCounterInitial;
    call_counter_ = 0;
  }

  // Computes the gain.
  void Compute(const std::array<float, kFftLengthBy2Plus1>& render_power,
               const SubtractorOutput& subtractor_output,
               std::span<const float> erl,
               size_t size_partitions,
               FftData* gain_fft) {
    const FftData& E = subtractor_output.E;
    const auto& E2 = subtractor_output.E2;
    FftData* G = gain_fft;
    const auto& X2 = render_power;
    ++call_counter_;
    if (++poor_excitation_counter_ < size_partitions ||
        call_counter_ <= size_partitions) {
      G->re.fill(0.f);
      G->im.fill(0.f);
    } else {
      std::array<float, kFftLengthBy2Plus1> mu;
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        if (X2[k] >= kNoiseGate) {
          mu[k] = H_error_[k] /
                  (0.5f * H_error_[k] * X2[k] + size_partitions * E2[k]);
        } else {
          mu[k] = 0.f;
        }
      }
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        H_error_[k] -= 0.5f * mu[k] * X2[k] * H_error_[k];
      }
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        G->re[k] = mu[k] * E.re[k];
        G->im[k] = mu[k] * E.im[k];
      }
    }
    const bool filter_ok = (subtractor_output.e2 <= 0.5f * subtractor_output.y2);
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      if (filter_ok) {
        H_error_[k] += kLeakageConverged * erl[k];
      } else {
        H_error_[k] += kLeakageDiverged * erl[k];
      }
      H_error_[k] = std::max(H_error_[k], kErrorFloor);
      H_error_[k] = std::min(H_error_[k], kErrorCeil);
    }
  }

  // 実行時設定変更は不要（定数化）。

  // 固定パラメータ
  static constexpr float kLeakageConverged = 0.00005f;
  static constexpr float kLeakageDiverged = 0.05f;
  static constexpr float kErrorFloor = 0.001f;
  static constexpr float kErrorCeil = 2.f;
  static constexpr float kNoiseGate = 20075344.f;
  static constexpr float kHErrorInitial = 10000.f;
  static constexpr int kPoorExcitationCounterInitial = 1000;
  std::array<float, kFftLengthBy2Plus1> H_error_;
  size_t poor_excitation_counter_;
  size_t call_counter_ = 0;
  
};


