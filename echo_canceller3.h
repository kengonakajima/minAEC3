// EchoCanceller3全体をまとめるクラス。
// 主に次の4点を担当する:
// - 64サンプルのモノラルブロックを受け取る。
// - エコーキャンセラ下層の処理へ同じブロックサイズで渡す。
// - Render/Capture API呼び出しのジッタを簡易的に吸収する。
// - 内部状態を保持しながらエコー除去を実行する。
//

struct EchoCanceller3 {
  std::deque<Block> render_transfer_queue_; // Render呼び出しから渡されるブロックを一時保持
  BlockProcessor block_processor_; // ブロック単位でAEC処理を実行するコア
  Block render_block_; // キューから取り出したレンダーデータのワーク領域
    
  EchoCanceller3()
      : render_transfer_queue_(),
        block_processor_(),
        render_block_() {}


  // レンダー信号を内部キューから取り込み、キャプチャ信号からエコーを除去する。
  void ProcessCapture(AudioBuffer* capture) {
    while (!render_transfer_queue_.empty()) {
      render_block_ = std::move(render_transfer_queue_.front());
      render_transfer_queue_.pop_front();
      block_processor_.BufferRender(render_block_);
    }
    Block capture_block;
    std::span<const float> cap_view(capture->mono_data_const(), kBlockSize);
    std::span<float, kBlockSize> cap_dst = capture_block.View();
    std::copy(cap_view.begin(), cap_view.end(), cap_dst.begin());
    block_processor_.ProcessCapture(&capture_block);
    float* out_ptr = capture->mono_data();
    std::span<float, kBlockSize> processed = capture_block.View();
    std::copy(processed.begin(), processed.end(), out_ptr);
  }

  // レンダー信号（64サンプル, モノラル）を解析して内部キューへ格納する。
  void AnalyzeRender(const AudioBuffer& render) {
    Block b;
    std::span<const float> buffer_view(render.mono_data_const(), kBlockSize);
    std::span<float, kBlockSize> v = b.View();
    std::copy(buffer_view.begin(), buffer_view.end(), v.begin());
    render_transfer_queue_.push_back(std::move(b));
    if (render_transfer_queue_.size() > 100) {
      render_transfer_queue_.pop_front();
    }
  }

  // 線形/非線形の有効・無効を設定（BlockProcessorへ委譲）
  void SetProcessingModes(bool enable_linear_filter, bool enable_nonlinear_suppressor) {
    block_processor_.SetProcessingModes(enable_linear_filter, enable_nonlinear_suppressor);
  }
  
};
 
