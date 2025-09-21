// エコー除去器が参照するレンダーデータをまとめて提供する。
struct RenderBuffer {
  const BlockBuffer* const block_buffer_; // 時間領域レンダーブロックのリングバッファ
  const SpectrumBuffer* const spectrum_buffer_; // レンダースペクトルのリングバッファ
  const FftBuffer* const fft_buffer_; // FFT済みレンダーデータのリングバッファ
    
  RenderBuffer(BlockBuffer* block_buffer,
               SpectrumBuffer* spectrum_buffer,
               FftBuffer* fft_buffer)
      : block_buffer_(block_buffer),
        spectrum_buffer_(spectrum_buffer),
        fft_buffer_(fft_buffer) {}

  // 指定オフセットの時間領域ブロックを取得する。
  const Block& GetBlock(int buffer_offset_blocks) const {
    int position =
        block_buffer_->OffsetIndex(block_buffer_->read, buffer_offset_blocks);
    return block_buffer_->buffer[position];
  }

  // 指定オフセットのスペクトルを取得する。
  const std::array<float, kFftLengthBy2Plus1>& Spectrum(
      int buffer_offset_ffts) const {
    int position = spectrum_buffer_->OffsetIndex(spectrum_buffer_->read,
                                                 buffer_offset_ffts);
    return spectrum_buffer_->buffer[position];
  }

  // FFT済みレンダーデータの全要素をspanで参照する。
  std::span<const FftData> GetFftBuffer() const { return fft_buffer_->buffer; }

  // 現在の読み出し位置を返す。
  size_t Position() const {
    return fft_buffer_->read;
  }

  // 指定数のスペクトルを合計してレンダーパワーを算出する。
  void SpectralSum(size_t num_spectra,
                   std::array<float, kFftLengthBy2Plus1>* X2) const {
    X2->fill(0.f);
    int position = spectrum_buffer_->read;
    for (size_t j = 0; j < num_spectra; ++j) {
      const auto& spectrum = spectrum_buffer_->buffer[position];
      for (size_t k = 0; k < X2->size(); ++k) {
        (*X2)[k] += spectrum[k];
      }
      position = spectrum_buffer_->IncIndex(position);
    }
  }

  // スペクトルバッファへの参照を返す。
  const SpectrumBuffer& GetSpectrumBuffer() const { return *spectrum_buffer_; }

};
