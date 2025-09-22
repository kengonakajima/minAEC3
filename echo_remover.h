// キャプチャ信号からエコー成分を除去する。
struct EchoRemover {
  const Aec3Fft fft_; // FFT処理を担うヘルパー
  Subtractor subtractor_; // 線形エコー推定・適応フィルタ
  SuppressionGain suppression_gain_; // 抑圧ゲイン計算器
  SuppressionFilter suppression_filter_; // 抑圧ゲイン適用フィルタ
  ResidualEchoEstimator residual_echo_estimator_; // 残留エコー推定器
  AecState aec_state_; // AEC全体の状態管理
  std::array<float, kFftLengthBy2> e_old_{}; // 残差信号の前ブロックを保持
  std::array<float, kFftLengthBy2> y_old_{}; // 入力信号の前ブロックを保持
  bool enable_linear_filter_ = true; // 線形減算を有効にするか
  bool enable_nonlinear_suppressor_ = true; // 非線形抑圧を有効にするか
  struct LastMetrics {
      float e2=0.f;
      float y2=0.f;
      float erle_avg=0.f;
      bool linear_usable=false;
  } last_metrics_;

    
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
  

  // キャプチャ信号1ブロックからエコー成分を除去する。
  // echo_path_variability: 遅延変化情報, render_buffer: レンダーバッファ, capture: キャプチャブロック
  void ProcessCapture(
      EchoPathVariability echo_path_variability,
      RenderBuffer* render_buffer,
      Block* capture) {
    Block* y = capture;
    std::array<float, kFftLengthBy2> e; // 残差信号
    std::array<float, kFftLengthBy2Plus1> Y2; // 入力信号のパワースペクトル
    std::array<float, kFftLengthBy2Plus1> E2; // 残差信号のパワースペクトル
    std::array<float, kFftLengthBy2Plus1> R2; // 残留エコー推定（パワースペクトル）
    std::array<float, kFftLengthBy2Plus1> S2_linear; // 線形推定エコーのパワースペクトル
    FftData Y; // 入力信号のFFT
    FftData E; // 残差信号のFFT
    SubtractorOutput subtractor_output; // 減算器の処理結果
    std::span<float, kBlockSize> capture_view = y->View();
    std::span<const float> capture_view_const(capture_view.data(), capture_view.size());

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
        std::copy(e.begin(), e.end(), capture_view.begin());
        return;
      }
    } else {
      // 線形無効: e = y として扱う（減算器は使わない）
      std::copy(capture_view.begin(), capture_view.end(), e.begin());
      // 減算出力と同等のメトリクスを作る（e=y として扱う → 非収束）
      for (size_t i = 0; i < subtractor_output.e.size(); ++i) {
        subtractor_output.e[i] = capture_view[i];
      }
      subtractor_output.ComputeMetrics(capture_view_const);
    }

    // 非線形用の共通前処理
    {
      std::span<const float> previous_block(y_old_.data(), y_old_.size());
      fft_.PaddedFft(capture_view_const, previous_block, &Y);
      std::copy(capture_view_const.begin(), capture_view_const.end(), y_old_.begin());
    }
    {
      std::span<const float> previous_error_block(e_old_.data(), e_old_.size());
      std::span<const float> error_view(e.data(), e.size());
      fft_.PaddedFft(error_view, previous_error_block, &E);
      std::copy(error_view.begin(), error_view.end(), e_old_.begin());
    }
    for (size_t k = 0; k < E.re.size(); ++k) {
      const float real_diff = Y.re[k] - E.re[k];
      const float imag_diff = Y.im[k] - E.im[k];
      S2_linear[k] = real_diff * real_diff + imag_diff * imag_diff;
    }
    Y.Spectrum(Y2);
    E.Spectrum(E2);
    aec_state_.Update(*render_buffer, E2, Y2, subtractor_output);

    const FftData& Y_fft = aec_state_.UsableLinearEstimate() ? E : Y;
    std::array<float, kFftLengthBy2Plus1> G;
    residual_echo_estimator_.Estimate(aec_state_, *render_buffer, S2_linear, Y2, &R2);
    if (aec_state_.UsableLinearEstimate()) {
      std::transform(E2.begin(), E2.end(), Y2.begin(), E2.begin(),
                     [](float a, float b) { return std::min(a, b); });
    }
    const std::array<float, kFftLengthBy2Plus1>& nearend_spectrum =
        aec_state_.UsableLinearEstimate() ? E2 : Y2;
    const std::array<float, kFftLengthBy2Plus1>& echo_spectrum =
        aec_state_.UsableLinearEstimate() ? S2_linear : R2;
    suppression_gain_.GetGain(nearend_spectrum, echo_spectrum, R2, &G);
    suppression_filter_.ApplyGain(G, Y_fft, y);

    // Metrics snapshot
    float erle_avg = 0.f;
    {
      const std::array<float, kFftLengthBy2Plus1>& erle = aec_state_.Erle();
      for (size_t i = 0; i < erle.size(); ++i) erle_avg += erle[i];
      erle_avg /= static_cast<float>(erle.size());
    }
    last_metrics_.e2 = subtractor_output.e2;
    last_metrics_.y2 = subtractor_output.y2;
    last_metrics_.erle_avg = erle_avg;
    last_metrics_.linear_usable = aec_state_.UsableLinearEstimate();
  }
  
};
