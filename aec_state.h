
// Echo remover の動作状態を管理。
struct AecState {
  AecState()
      : filter_quality_state_(),
        erle_estimator_(2 * kNumBlocksPerSecond),
        subtractor_output_analyzer_() {}
  

  // エコー減算器の線形推定が残留エコー推定に利用できるかを返す。
  bool UsableLinearEstimate() const { return filter_quality_state_.LinearFilterUsable(); }

  const std::array<float, kFftLengthBy2Plus1>& Erle() const {
    return erle_estimator_.Erle();
  }

  // 線形フィルタに基づく遅延推定値を返す（簡略化により常に0）。
  int MinDirectPathFilterDelay() const { return 0; }

  // エコーパス変化時に必要なリセット処理を行う。
  void HandleEchoPathChange(const EchoPathVariability& echo_path_variability) {
    if (echo_path_variability.delay_change != EchoPathVariability::kNone) {
      erle_estimator_.Reset(true);
      filter_quality_state_.Reset();
    }
    subtractor_output_analyzer_.HandleEchoPathChange();
  }

  // 最新の信号情報でAEC状態を更新する。
  void Update(const RenderBuffer& render_buffer,
              const std::array<float, kFftLengthBy2Plus1>& E2,
              const std::array<float, kFftLengthBy2Plus1>& Y2,
              const SubtractorOutput& subtractor_output) {
    bool any_filter_converged;
    subtractor_output_analyzer_.Update(subtractor_output, &any_filter_converged);
    const Block& aligned_render_block = render_buffer.GetBlock(0);
    const float render_energy = std::inner_product(
        aligned_render_block.begin(), aligned_render_block.end(),
        aligned_render_block.begin(), 0.f);
    const bool active_render = render_energy > (100.f * 100.f) * kFftLengthBy2;
    erle_estimator_.Update(Y2, E2);
    filter_quality_state_.Update(active_render, any_filter_converged);
  }

  // 線形フィルタの適用可否を評価し、エコー抑圧処理の制御に用いる補助クラス。
  struct FilteringQualityAnalyzer {
    FilteringQualityAnalyzer() {}

    // 線形フィルタをエコーキャンセラ出力に利用できると判断したかを返す。
    bool LinearFilterUsable() const { return overall_usable_linear_estimates_; }

    // 評価状態をリセットする。
    void Reset() {
      overall_usable_linear_estimates_ = false;
      filter_update_blocks_since_reset_ = 0;
    }

    // 新しいデータに基づいて評価結果を更新する。
    void Update(bool active_render, bool any_filter_converged) {
      const bool filter_update = active_render;
      filter_update_blocks_since_reset_ += filter_update ? 1 : 0;
      filter_update_blocks_since_start_ += filter_update ? 1 : 0;
      convergence_seen_ = convergence_seen_ || any_filter_converged;
      const bool sufficient_data_to_converge_at_startup =
          filter_update_blocks_since_start_ > kNumBlocksPerSecond * 0.4f;
      const bool sufficient_data_to_converge_at_reset =
          sufficient_data_to_converge_at_startup &&
          filter_update_blocks_since_reset_ > kNumBlocksPerSecond * 0.2f;
      overall_usable_linear_estimates_ =
          sufficient_data_to_converge_at_startup &&
          sufficient_data_to_converge_at_reset;
      overall_usable_linear_estimates_ =
          overall_usable_linear_estimates_ && convergence_seen_;
    }

   
   bool overall_usable_linear_estimates_ = false;
    size_t filter_update_blocks_since_reset_ = 0;
    size_t filter_update_blocks_since_start_ = 0;
    bool convergence_seen_ = false;
  } filter_quality_state_;

  ErleEstimator erle_estimator_;
  SubtractorOutputAnalyzer subtractor_output_analyzer_;
};
