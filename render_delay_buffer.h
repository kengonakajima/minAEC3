// レンダーブロックを遅延付きで保持し、指定遅延で取り出せるようにする。
struct RenderDelayBuffer {
  enum BufferingEvent {
      kNone, // イベントなし
      kRenderUnderrun, // レンダー不足（読み出し側が先行）
      kRenderOverrun // レンダー過多（書き込み側が先行）
  };

  inline static constexpr size_t kDownSamplingFactor = 4; // 遅延推定で用いるダウンサンプリング倍率
  const int sub_block_size_; // ダウンサンプル後のサブブロック長
  BlockBuffer blocks_; // レンダーブロックのリングバッファ
  SpectrumBuffer spectra_; // レンダースペクトルのリングバッファ
  FftBuffer ffts_; // FFT済みレンダーデータのリングバッファ
  int delay_; // 現在適用中の遅延（ブロック単位）
  RenderBuffer echo_remover_buffer_; // EchoRemoverへ渡すバッファビュー
  DownsampledRenderBuffer low_rate_; // ダウンサンプリング済みレンダーデータ
  const Aec3Fft fft_; // FFT処理ヘルパー
  std::vector<float> render_ds_; // ダウンサンプル用ワーク領域
  const int buffer_headroom_; // バッファの安全余裕ブロック数
    
  RenderDelayBuffer()
      : sub_block_size_(static_cast<int>(kBlockSize / kDownSamplingFactor)),
        blocks_(GetRenderDelayBufferSize(kDownSamplingFactor,
                                         /*num_filters=*/5,
                                         /*filter_length_blocks=*/13)),
        spectra_(blocks_.buffer.size()),
        ffts_(blocks_.buffer.size()),
        delay_(-1),
        echo_remover_buffer_(&blocks_, &spectra_, &ffts_),
        low_rate_(GetDownSampledBufferSize(kDownSamplingFactor,
                                           /*num_filters=*/5)),
        fft_(),
        render_ds_(sub_block_size_, 0.f),
        buffer_headroom_(13) {
    Reset();
  }
  

  // バッファの整列状態をリセットする。
  void Reset() {
    low_rate_.read = low_rate_.OffsetIndex(low_rate_.write, sub_block_size_);
    ApplyTotalDelay(/*default_delay_blocks=*/10);
    delay_ = -1;
  }

  // レンダーブロックをバッファへ挿入する。
  BufferingEvent Insert(const Block& block) {
    const int previous_write = blocks_.write;
    IncrementWriteIndices();
    BufferingEvent event = RenderOverrun() ? kRenderOverrun : kNone;
    InsertBlock(block, previous_write);
    if (event != kNone) {
      Reset();
    }
    return event;
  }

  // バッファを1ステップ進め、特殊イベントの有無を返す。
  BufferingEvent PrepareCaptureProcessing() {
    BufferingEvent event = kNone;
    if (RenderUnderrun()) {
      IncrementReadIndices();
      if (delay_ > 0) delay_ = delay_ - 1;
      event = kRenderUnderrun;
    } else {
      IncrementLowRateReadIndices();
      IncrementReadIndices();
    }
    return event;
  }


  // 遅延量を設定し、変更があったかどうかを返す。
  bool AlignFromDelay(size_t delay) {
    if (delay_ == static_cast<int>(delay)) {
      return false;
    }
    delay_ = static_cast<int>(delay);
    int total_delay = MapDelayToTotalDelay(delay_);
    total_delay = static_cast<int>(std::min(MaxDelay(), static_cast<size_t>(std::max(total_delay, 0))));
    ApplyTotalDelay(total_delay);
    return true;
  }


  // 適用可能な最大遅延を返す。
  size_t MaxDelay() const { return blocks_.buffer.size() - 1 - buffer_headroom_; }

  // EchoRemover用レンダーバッファを取得する。
  RenderBuffer* GetRenderBuffer() { return &echo_remover_buffer_; }

  // ダウンサンプリング済みレンダーバッファを取得する。
  const DownsampledRenderBuffer& GetDownsampledRenderBuffer() const { return low_rate_; }

  int BufferLatency() const {
    const DownsampledRenderBuffer& l = low_rate_;
    int latency_samples = (l.buffer.size() + l.read - l.write) % l.buffer.size();
    int latency_blocks = latency_samples / sub_block_size_;
    return latency_blocks;
  }

  int MapDelayToTotalDelay(int external_delay_blocks) const {
    const int latency_blocks = BufferLatency();
    return latency_blocks + external_delay_blocks;
  }
  void ApplyTotalDelay(int delay) {
    blocks_.read = blocks_.OffsetIndex(blocks_.write, -delay);
    spectra_.read = spectra_.OffsetIndex(spectra_.write, delay);
    ffts_.read = ffts_.OffsetIndex(ffts_.write, delay);
  }
  // ブロックを挿入して各種バッファを更新する。
  // block: 追加するレンダーブロック, previous_write: 更新前のwrite位置
  void InsertBlock(const Block& block, int previous_write) {
    BlockBuffer& b = blocks_;
    DownsampledRenderBuffer& lr = low_rate_;
    std::vector<float>& ds = render_ds_;
    FftBuffer& f = ffts_;
    SpectrumBuffer& s = spectra_;
    std::copy(block.begin(), block.end(), b.buffer[b.write].begin());
    DecimateBy4(b.buffer[b.write].View(), ds);
    std::copy(ds.rbegin(), ds.rend(), lr.buffer.begin() + lr.write);
    fft_.PaddedFft(b.buffer[b.write].View(),
                   b.buffer[previous_write].View(),
                   &f.buffer[f.write]);
    f.buffer[f.write].Spectrum(s.buffer[s.write]);
  }
  void IncrementWriteIndices() {
    low_rate_.UpdateWriteIndex(-sub_block_size_);
    blocks_.IncWriteIndex();
    spectra_.DecWriteIndex();
    ffts_.DecWriteIndex();
  }
  void IncrementLowRateReadIndices() { low_rate_.UpdateReadIndex(-sub_block_size_); }
  void IncrementReadIndices() {
    if (blocks_.read != blocks_.write) {
      blocks_.IncReadIndex();
      spectra_.DecReadIndex();
      ffts_.DecReadIndex();
    }
  }
  bool RenderOverrun() { return low_rate_.read == low_rate_.write || blocks_.read == blocks_.write; }
  bool RenderUnderrun() { return low_rate_.read == low_rate_.write; }
};
