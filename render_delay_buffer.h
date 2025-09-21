#include <stddef.h>
#include <vector>
#include <algorithm>
 

// Buffers incoming render blocks and allows extraction with a specified delay.
struct RenderDelayBuffer {
  enum BufferingEvent {
    kNone,
    kRenderUnderrun,
    kRenderOverrun
  };

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
  

  // Resets the buffer alignment.
  void Reset() {
    low_rate_.read = low_rate_.OffsetIndex(low_rate_.write, sub_block_size_);
    ApplyTotalDelay(/*default_delay_blocks=*/10);
    delay_ = -1;
  }

  // Inserts a block into the buffer.
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

  // Updates the buffers one step based on the specified buffer delay. Returns
  // an enum indicating whether there was a special event that occurred.
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


  // Sets the buffer delay and returns a bool indicating whether the delay
  // changed.
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


  // Gets the buffer max delay.
  size_t MaxDelay() const { return blocks_.buffer.size() - 1 - buffer_headroom_; }

  // Returns the render buffer for the echo remover.
  RenderBuffer* GetRenderBuffer() { return &echo_remover_buffer_; }

  // Returns the downsampled render buffer.
  const DownsampledRenderBuffer& GetDownsampledRenderBuffer() const { return low_rate_; }

  int BufferLatency() const {
    const DownsampledRenderBuffer& l = low_rate_;
    int latency_samples = (l.buffer.size() + l.read - l.write) % l.buffer.size();
    int latency_blocks = latency_samples / sub_block_size_;
    return latency_blocks;
  }

  inline static constexpr size_t kDownSamplingFactor = 4;
  const int sub_block_size_;
  BlockBuffer blocks_;
  SpectrumBuffer spectra_;
  FftBuffer ffts_;
  int delay_;
  RenderBuffer echo_remover_buffer_;
  DownsampledRenderBuffer low_rate_;
  const Aec3Fft fft_;
  std::vector<float> render_ds_;
  const int buffer_headroom_;

  int MapDelayToTotalDelay(int external_delay_blocks) const {
    const int latency_blocks = BufferLatency();
    return latency_blocks + external_delay_blocks;
  }
  void ApplyTotalDelay(int delay) {
    blocks_.read = blocks_.OffsetIndex(blocks_.write, -delay);
    spectra_.read = spectra_.OffsetIndex(spectra_.write, delay);
    ffts_.read = ffts_.OffsetIndex(ffts_.write, delay);
  }
  void InsertBlock(const Block& block, int previous_write) {
    auto& b = blocks_;
    auto& lr = low_rate_;
    auto& ds = render_ds_;
    auto& f = ffts_;
    auto& s = spectra_;
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


