#include <array>
#include <algorithm>
#include <span>
 

// Removes the echo from the capture signal.
struct EchoRemover {
  EchoRemover()
      : fft_(),
        subtractor_(),
        suppression_gain_(),
        suppression_filter_(),
        residual_echo_estimator_(),
        aec_state_() {}

  // 線形/非線形の有効・無効を設定
  void SetProcessingModes(bool enable_linear_filter,
                          bool enable_nonlinear_suppressor) {
    enable_linear_filter_ = enable_linear_filter;
    enable_nonlinear_suppressor_ = enable_nonlinear_suppressor;
  }
  

  // Removes the echo from a block of samples from the capture signal. The
  // supplied render signal is assumed to be pre-aligned with the capture
  // signal.
  void ProcessCapture(
      EchoPathVariability echo_path_variability,
      RenderBuffer* render_buffer,
      Block* capture) {
    Block* y = capture;
    std::array<float, kFftLengthBy2> e;
    std::array<float, kFftLengthBy2Plus1> Y2;
    std::array<float, kFftLengthBy2Plus1> E2;
    std::array<float, kFftLengthBy2Plus1> R2;
    std::array<float, kFftLengthBy2Plus1> S2_linear;
    FftData Y;
    FftData E;
    SubtractorOutput subtractor_output;

    auto LinearEchoPower = [](const FftData& E,
                              const FftData& Y,
                              std::array<float, kFftLengthBy2Plus1>* S2) {
      for (size_t k = 0; k < E.re.size(); ++k) {
        (*S2)[k] = (Y.re[k] - E.re[k]) * (Y.re[k] - E.re[k]) +
                   (Y.im[k] - E.im[k]) * (Y.im[k] - E.im[k]);
      }
    };
    auto WindowedPaddedFft = [&](const Aec3Fft& fft,
                                 std::span<const float> v,
                                 std::span<float> v_old,
                                 FftData* V) {
      fft.PaddedFft(v, v_old, V);
      std::copy(v.begin(), v.end(), v_old.begin());
    };

    if (echo_path_variability.DelayChanged()) {
      subtractor_.HandleEchoPathChange(echo_path_variability);
      aec_state_.HandleEchoPathChange(echo_path_variability);
    }

    // 何も使わない（両方無効）の場合は素通し
    if (!enable_linear_filter_ && !enable_nonlinear_suppressor_) {
      return;  // yはそのまま
    }

    if (enable_linear_filter_) {
      // 線形フィルタ有効
      subtractor_.Process(*render_buffer, *y, aec_state_, &subtractor_output);
      std::copy(subtractor_output.e.begin(), subtractor_output.e.end(), e.begin());

      if (!enable_nonlinear_suppressor_) {
        // 線形のみ: e をそのまま時間領域出力にする
        auto dst = y->View();
        std::copy(e.begin(), e.end(), dst.begin());
        return;
      }
    } else {
      // 線形無効: e = y として扱う（減算器は使わない）
      auto yv = y->View();
      std::copy(yv.begin(), yv.end(), e.begin());
      // 減算出力と同等のメトリクスを作る（e=y として扱う → 非収束）
      for (size_t i = 0; i < subtractor_output.e.size(); ++i) {
        subtractor_output.e[i] = yv[i];
      }
      subtractor_output.ComputeMetrics(yv);
    }

    // 非線形用の共通前処理
    WindowedPaddedFft(fft_, y->View(), y_old_, &Y);
    WindowedPaddedFft(fft_, e, e_old_, &E);
    LinearEchoPower(E, Y, &S2_linear);
    Y.Spectrum(Y2);
    E.Spectrum(E2);
    aec_state_.Update(*render_buffer, E2, Y2, subtractor_output);

    const auto& Y_fft = aec_state_.UsableLinearEstimate() ? E : Y;
    std::array<float, kFftLengthBy2Plus1> G;
    residual_echo_estimator_.Estimate(aec_state_, *render_buffer, S2_linear, Y2, &R2);
    if (aec_state_.UsableLinearEstimate()) {
      std::transform(E2.begin(), E2.end(), Y2.begin(), E2.begin(),
                     [](float a, float b) { return std::min(a, b); });
    }
    const auto& nearend_spectrum = aec_state_.UsableLinearEstimate() ? E2 : Y2;
    const auto& echo_spectrum = aec_state_.UsableLinearEstimate() ? S2_linear : R2;
    suppression_gain_.GetGain(nearend_spectrum, echo_spectrum, R2, &G);
    suppression_filter_.ApplyGain(G, Y_fft, y);

    // Metrics snapshot
    float erle_avg = 0.f;
    {
      const auto& erle = aec_state_.Erle();
      for (size_t i = 0; i < erle.size(); ++i) erle_avg += erle[i];
      erle_avg /= static_cast<float>(erle.size());
    }
    last_metrics_.e2 = subtractor_output.e2;
    last_metrics_.y2 = subtractor_output.y2;
    last_metrics_.erle_avg = erle_avg;
    last_metrics_.linear_usable = aec_state_.UsableLinearEstimate();
  }

  const Aec3Fft fft_;
  Subtractor subtractor_;
  SuppressionGain suppression_gain_;
  SuppressionFilter suppression_filter_;
  ResidualEchoEstimator residual_echo_estimator_;
  AecState aec_state_;
  std::array<float, kFftLengthBy2> e_old_{};
  std::array<float, kFftLengthBy2> y_old_{};
  bool enable_linear_filter_ = true;
  bool enable_nonlinear_suppressor_ = true;
  struct LastMetrics { float e2=0.f; float y2=0.f; float erle_avg=0.f; bool linear_usable=false; } last_metrics_;
  
};
