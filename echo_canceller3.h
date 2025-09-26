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
  void ProcessBlock(Block* capture, const Block* render) {
    if (render) {
      render_transfer_queue_.push_back(*render);
      if (render_transfer_queue_.size() > 100) {
        render_transfer_queue_.pop_front();
      }
    }

    while (!render_transfer_queue_.empty()) {
      const Block& pending_render = render_transfer_queue_.front();
      block_processor_.BufferRender(pending_render);
      render_transfer_queue_.pop_front();
    }

    block_processor_.ProcessCapture(capture);
  }

  // 線形/非線形の有効・無効を設定（BlockProcessorへ委譲）
  void SetProcessingModes(bool enable_linear_filter, bool enable_nonlinear_suppressor) {
    block_processor_.SetProcessingModes(enable_linear_filter, enable_nonlinear_suppressor);
  }

  const EchoRemover::LastMetrics& GetLastMetrics() const {
    return block_processor_.GetLastMetrics();
  }
  
};
 
