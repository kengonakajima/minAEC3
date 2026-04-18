// キャプチャ信号からエコー成分を除去する。
struct EchoRemover {
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
      float output_e2=0.f;
      bool valid=false;
  } last_metrics_;

    
  EchoRemover()
      : subtractor_(),
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

  const LastMetrics& last_metrics() const { return last_metrics_; }
  

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
    std::span<float, kBlockSize> capture_view(*y);
    std::span<const float> capture_view_const(capture_view.data(), capture_view.size());

    last_metrics_.valid = false;

    if (echo_path_variability != EchoPathVariability::kNone) {
      subtractor_.HandleEchoPathChange(echo_path_variability);
      aec_state_.HandleEchoPathChange(echo_path_variability);
    }

    // 何も使わない（両方無効）の場合は素通し
    if (!enable_linear_filter_ && !enable_nonlinear_suppressor_) {
      float bypass_energy = 0.f;
      for (const float sample : capture_view_const) {
        bypass_energy += sample * sample;
      }
      last_metrics_.y2 = bypass_energy;
      last_metrics_.e2 = bypass_energy;
      last_metrics_.output_e2 = bypass_energy;
      last_metrics_.erle_avg = 1.f;
      last_metrics_.linear_usable = false;
      last_metrics_.valid = true;
      return;  // yはそのまま
    }

    if (enable_linear_filter_) {
      // 線形フィルタ有効
      subtractor_.Process(*render_buffer, *y, aec_state_, &subtractor_output);
      std::copy(subtractor_output.e.begin(), subtractor_output.e.end(), e.begin());

      if (!enable_nonlinear_suppressor_) {
        // 線形のみ: e をそのまま時間領域出力にする
        std::copy(e.begin(), e.end(), capture_view.begin());
        constexpr float kEps = 1e-9f;
        last_metrics_.y2 = subtractor_output.y2;
        last_metrics_.e2 = subtractor_output.e2;
        last_metrics_.output_e2 = subtractor_output.e2;
        const float ratio =
            (subtractor_output.e2 + kEps) > 0.f
                ? subtractor_output.y2 / (subtractor_output.e2 + kEps)
                : 1.f;
        last_metrics_.erle_avg = ratio;
        last_metrics_.linear_usable = aec_state_.UsableLinearEstimate();
        last_metrics_.valid = true;
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
      PaddedFft(capture_view_const, previous_block, &Y);
      std::copy(capture_view_const.begin(), capture_view_const.end(), y_old_.begin());
    }
    {
      std::span<const float> previous_error_block(e_old_.data(), e_old_.size());
      std::span<const float> error_view(e.data(), e.size());
      PaddedFft(error_view, previous_error_block, &E);
      std::copy(error_view.begin(), error_view.end(), e_old_.begin());
    }
    for (size_t k = 0; k < E.re.size(); ++k) {
      const float real_diff = Y.re[k] - E.re[k];
      const float imag_diff = Y.im[k] - E.im[k];
      S2_linear[k] = real_diff * real_diff + imag_diff * imag_diff;
    }
    Y.Spectrum(Y2);
    E.Spectrum(E2);
    aec_state_.Update(E2, Y2);

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
    float output_energy = 0.f;
    for (const float sample : capture_view) {
      output_energy += sample * sample;
    }
    last_metrics_.output_e2 = output_energy;
    last_metrics_.valid = true;
  }
  
};

// Performs echo cancellation on 64-sample blocks.
struct BlockProcessor {
  BlockProcessor()
      : render_buffer_(),
        delay_estimator_(),
        echo_remover_(),
        render_event_(RenderDelayBuffer::kNone) {}
  

  void ProcessCapture(Block* capture_block) {
    if (render_properly_started_) {
      if (!capture_properly_started_) {
        capture_properly_started_ = true;
        render_buffer_.Reset();
        delay_estimator_.Reset();
      }
    } else {
      return;
    }

    EchoPathVariability echo_path_variability = EchoPathVariability::kNone;
    if (render_event_ == RenderDelayBuffer::kRenderOverrun && render_properly_started_) {
      echo_path_variability = EchoPathVariability::kBufferFlush;
      delay_estimator_.Reset();
    }
    render_event_ = RenderDelayBuffer::kNone;

    RenderDelayBuffer::BufferingEvent buffer_event = render_buffer_.PrepareCaptureProcessing();
    if (buffer_event == RenderDelayBuffer::kRenderUnderrun) {
      delay_estimator_.Reset();
    }

    int d_samples = delay_estimator_.EstimateDelay(render_buffer_.GetDownsampledRenderBuffer(), *capture_block);
    if (d_samples >= 0) {
        estimated_delay_blocks_ = static_cast<int>(d_samples >> kBlockSizeLog2);
    } else {
        estimated_delay_blocks_ = -1;
    }

    if (estimated_delay_blocks_ >= 0) {
      bool delay_change = render_buffer_.AlignFromDelay(static_cast<size_t>(estimated_delay_blocks_));
      if (delay_change) {
        echo_path_variability = EchoPathVariability::kNewDetectedDelay;
      }
    }

    echo_remover_.ProcessCapture(echo_path_variability,
                                 render_buffer_.GetRenderBuffer(),
                                 capture_block);
  }

  // Buffers a 64-sample render block (directly supplied by the caller).
  void BufferRender(const Block& render_block) {
    render_event_ = render_buffer_.Insert(render_block);
    render_properly_started_ = true;
  }

  // 線形/非線形の有効・無効を設定（EchoRemoverへ委譲）
  void SetProcessingModes(bool enable_linear_filter,
                          bool enable_nonlinear_suppressor) {
    echo_remover_.SetProcessingModes(enable_linear_filter, enable_nonlinear_suppressor);
  }

  const EchoRemover::LastMetrics& GetLastMetrics() const {
    return echo_remover_.last_metrics();
  }

  bool capture_properly_started_ = false;
  bool render_properly_started_ = false;
  RenderDelayBuffer render_buffer_;
  EchoPathDelayEstimator delay_estimator_;
  EchoRemover echo_remover_;
  RenderDelayBuffer::BufferingEvent render_event_;
  int estimated_delay_blocks_ = -1;
};
