
// Echo remover の動作状態を管理。
struct AecState {
  AecState()
      : filter_quality_state_(),
        erle_estimator_(2 * kNumBlocksPerSecond),
        subtractor_output_analyzer_() {}
  

  // Returns whether the echo subtractor can be used to determine the residual
  // echo.
  bool UsableLinearEstimate() const { return filter_quality_state_.LinearFilterUsable(); }

  const std::array<float, kFftLengthBy2Plus1>& Erle() const {
    return erle_estimator_.Erle();
  }

  // Returns the delay estimate based on the linear filter.
  int MinDirectPathFilterDelay() const { return 0; }

  // Takes appropriate action at an echo path change.
  void HandleEchoPathChange(const EchoPathVariability& echo_path_variability) {
    const auto full_reset = [&]() {
      erle_estimator_.Reset(true);
      filter_quality_state_.Reset();
    };
    if (echo_path_variability.delay_change != EchoPathVariability::kNone) {
      full_reset();
    }
    subtractor_output_analyzer_.HandleEchoPathChange();
  }

  // Updates the aec state.
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

  // Class for analyzing how well the linear filter is, and can be expected to,
  // perform on the current signals. The purpose of this is for using to
  // select the echo suppression functionality as well as the input to the echo
  // suppressor.
  struct FilteringQualityAnalyzer {
    FilteringQualityAnalyzer() {}

    // Returns whether the linear filter can be used for the echo
    // canceller output.
    bool LinearFilterUsable() const { return overall_usable_linear_estimates_; }

    // Resets the state of the analyzer.
    void Reset() {
      overall_usable_linear_estimates_ = false;
      filter_update_blocks_since_reset_ = 0;
    }

    // Updates the analysis based on new data.
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

