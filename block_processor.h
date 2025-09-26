
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

    EchoPathVariability echo_path_variability(EchoPathVariability::kNone);
    if (render_event_ == RenderDelayBuffer::kRenderOverrun && render_properly_started_) {
      echo_path_variability.delay_change = EchoPathVariability::kBufferFlush;
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
        echo_path_variability.delay_change = EchoPathVariability::kNewDetectedDelay;
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
