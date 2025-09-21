#include <stddef.h>
#include <algorithm>
#include <deque>
#include <span>

 

// Main class for the echo canceller3.
// It does 4 things:
// -Receives 64-sample mono blocks.
// -Provides the lower level echo canceller functionality with the same
//   64-sample block size throughout.
// -Partially handles the jitter in the render and capture API
// call sequence.
//

struct EchoCanceller3 {

  EchoCanceller3()
      : render_transfer_queue_(),
        block_processor_(),
        render_block_(),
        capture_block_() {}



  // Analyzes and stores an internal copy of the render signal (mono, 64-sample).
  // Processes the capture signal (mono, 64-sample) to remove echo.
  void ProcessCapture(AudioBuffer* capture) {
    while (!render_transfer_queue_.empty()) {
      render_block_ = std::move(render_transfer_queue_.front());
      render_transfer_queue_.pop_front();
      block_processor_.BufferRender(render_block_);
    }
    std::span<const float> cap_view(capture->mono_data_const(), kBlockSize);
    auto cap_dst = capture_block_.View();
    std::copy(cap_view.begin(), cap_view.end(), cap_dst.begin());
    block_processor_.ProcessCapture(&capture_block_);
    float* out_ptr = capture->mono_data();
    auto after = capture_block_.View();
    std::copy(after.begin(), after.end(), out_ptr);
  }

  // Analyzes and stores an internal copy of the render signal (mono, 64-sample).
  void AnalyzeRender(const AudioBuffer& render) {
    Block b;
    std::span<const float> buffer_view(render.mono_data_const(), kBlockSize);
    auto v = b.View();
    std::copy(buffer_view.begin(), buffer_view.end(), v.begin());
    render_transfer_queue_.push_back(std::move(b));
    if (render_transfer_queue_.size() > 100) {
      render_transfer_queue_.pop_front();
    }
  }

  // 線形/非線形の有効・無効を設定（BlockProcessorへ委譲）
  void SetProcessingModes(bool enable_linear_filter,
                          bool enable_nonlinear_suppressor) {
    block_processor_.SetProcessingModes(enable_linear_filter,
                                        enable_nonlinear_suppressor);
  }

  std::deque<Block> render_transfer_queue_;
  BlockProcessor block_processor_;
  Block render_block_;
  Block capture_block_;
  
};
 

