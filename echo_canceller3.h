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
    
  EchoCanceller3()
      : render_transfer_queue_(),
        block_processor_() {}


  // レンダー信号を取り込みつつキャプチャ信号からエコーを除去する。
  // render が nullptr の場合は、直前にバッファ済みのレンダーブロックのみを利用する。
  void ProcessBlock(AudioBuffer* capture, const AudioBuffer* render) {
    if (render) {
      Block render_block;
      std::span<const float> buffer_view(render->mono_data_const(), kBlockSize);
      std::span<float, kBlockSize> render_view = render_block.View();
      std::copy(buffer_view.begin(), buffer_view.end(), render_view.begin());
      render_transfer_queue_.push_back(std::move(render_block));
      if (render_transfer_queue_.size() > 100) {
        render_transfer_queue_.pop_front();
      }
    }

    while (!render_transfer_queue_.empty()) {
      const Block& pending_render = render_transfer_queue_.front();
      block_processor_.BufferRender(pending_render);
      render_transfer_queue_.pop_front();
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

  // 線形/非線形の有効・無効を設定（BlockProcessorへ委譲）
  void SetProcessingModes(bool enable_linear_filter, bool enable_nonlinear_suppressor) {
    block_processor_.SetProcessingModes(enable_linear_filter, enable_nonlinear_suppressor);
  }
  
};
 
