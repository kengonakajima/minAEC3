// 自己回帰型の線形フィルタ更新ゲインを計算する。
struct FilterUpdateGain {

  static constexpr float kLeakageConverged = 0.00005f; // 収束時に誤差推定へ加えるリーク係数
  static constexpr float kLeakageDiverged = 0.05f; // 発散時に誤差推定へ加えるリーク係数
  static constexpr float kErrorFloor = 0.001f; // 誤差推定H_error_の下限値
  static constexpr float kErrorCeil = 2.f; // 誤差推定H_error_の上限値
  static constexpr float kNoiseGate = 20075344.f; // レンダーパワーがこの値未満なら更新を抑制
  static constexpr float kHErrorInitial = 10000.f; // 誤差推定の初期値
  static constexpr int kPoorExcitationCounterInitial = 1000; // 低励起状態の判定用カウンタ初期値
  std::array<float, kFftLengthBy2Plus1> H_error_; // 各周波数ビンのフィルタ誤差推定値
  size_t poor_excitation_counter_; // 励起不足状態が続いたフレーム数
  size_t call_counter_ = 0; // Computeを呼び出した累計回数
    
  FilterUpdateGain()
      : poor_excitation_counter_(kPoorExcitationCounterInitial) {
    H_error_.fill(kHErrorInitial);
  }
  


  // 既知のエコーパス変化が発生した際のリセット処理。
  void HandleEchoPathChange(const EchoPathVariability& echo_path_variability) {
    if (echo_path_variability.delay_change != EchoPathVariability::kNone) {
      H_error_.fill(kHErrorInitial);
    }
    poor_excitation_counter_ = kPoorExcitationCounterInitial;
    call_counter_ = 0;
  }

  // 更新ゲインを計算する。
  // render_power: レンダー信号パワー, subtractor_output: 減算器の出力統計, erl: 推定ERL, size_partitions: パーティション数, gain_fft: 出力先
  void Compute(const std::array<float, kFftLengthBy2Plus1>& render_power,
               const SubtractorOutput& subtractor_output,
               std::span<const float> erl,
               size_t size_partitions,
               FftData* gain_fft) {
    const FftData& E = subtractor_output.E;
    const std::array<float, kFftLengthBy2Plus1>& E2 = subtractor_output.E2;
    FftData* G = gain_fft;
    const std::array<float, kFftLengthBy2Plus1>& X2 = render_power;
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
};

