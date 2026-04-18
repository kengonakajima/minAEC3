// 2次元ベクトル（ここではBlock）を保持するリングバッファと読み書きインデックスをまとめた構造体。
struct BlockBuffer {
  const int size; // バッファ内のBlock数（固定長）
  std::vector<Block> buffer; // 実データを保持するリングバッファ
  int write = 0; // 次に書き込む位置
  int read = 0; // 次に読み出す位置
    
  BlockBuffer(size_t size) : size(static_cast<int>(size)), buffer(size, Block()) {}
  int OffsetIndex(int index, int offset) const { return ::OffsetIndex(index, offset, size); }
  void IncWriteIndex() { write = ::IncIndex(write, size); }
  void IncReadIndex() { read = ::IncIndex(read, size); }
};
// 128ポイント実数FFTで生成された複素数データを保持する構造体。kFftLengthBy2Plus1は65。128ポイントの末尾63要素は対称なので不要
struct FftData {
  std::array<float, kFftLengthBy2Plus1> re; // 実数部分
  std::array<float, kFftLengthBy2Plus1> im; // 虚数部分
    
  // 虚数部をすべてクリアする。
  void Clear() {
    re.fill(0.f);
    im.fill(0.f);
  }

  // データのパワースペクトルを計算する。
  void Spectrum(std::span<float> power_spectrum) const {
    std::transform(re.begin(), re.end(), im.begin(), power_spectrum.begin(),
                   [](float a, float b) { return a * a + b * b; });
  }

  // 配列入力からデータをコピーする。
  void CopyFromPackedArray(const std::array<float, kFftLength>& v) {
    re[0] = v[0];
    re[kFftLengthBy2] = v[1];
    im[0] = im[kFftLengthBy2] = 0;
    for (size_t k = 1, j = 2; k < kFftLengthBy2; ++k) {
      re[k] = v[j++];
      im[k] = v[j++];
    }
  }

  // データを配列へコピーする。
  void CopyToPackedArray(std::array<float, kFftLength>* v) const {
    
    (*v)[0] = re[0];
    (*v)[1] = re[kFftLengthBy2];
    for (size_t k = 1, j = 2; k < kFftLengthBy2; ++k) {
      (*v)[j++] = re[k];
      (*v)[j++] = im[k];
    }
  }
};

 

// FftData のリングバッファと読み書きインデックスをまとめた構造体。
struct FftBuffer {
  const int size; // バッファ長（保持するFftDataの数）
  std::vector<FftData> buffer; // FftDataのリングバッファ
  int write = 0; // 次に書き込む位置
  int read = 0; // 次に読み出す位置
    
  FftBuffer(size_t size) : size(static_cast<int>(size)), buffer(size) {
    for (FftData& fft_data : buffer) fft_data.Clear();
  }

  int OffsetIndex(int index, int offset) const { return ::OffsetIndex(index, offset, size); }
  void DecWriteIndex() { write = (write > 0 ? write - 1 : size - 1); }
  void DecReadIndex() { read = (read > 0 ? read - 1 : size - 1); }
};

 
// 1次元スペクトル（配列）を保持するリングバッファと読み書きインデックスのラッパー。
struct SpectrumBuffer {
  const int size; // バッファ長（保持するスペクトル個数）
  std::vector<std::array<float, kFftLengthBy2Plus1>> buffer;  // 各周波数ビンのスペクトル値
  int write = 0; // 次に書き込むスペクトルの位置
  int read = 0; // 次に読み出すスペクトルの位置
    
  SpectrumBuffer(size_t size)
      : size(static_cast<int>(size)), buffer(size) {
    for (std::array<float, kFftLengthBy2Plus1>& c : buffer) {
      std::fill(c.begin(), c.end(), 0.f);
    }
  }

  int IncIndex(int index) const { return ::IncIndex(index, size); }
  int OffsetIndex(int index, int offset) const { return ::OffsetIndex(index, offset, size); }
  void DecWriteIndex() { write = (write > 0 ? write - 1 : size - 1); }
  void DecReadIndex() { read = (read > 0 ? read - 1 : size - 1); }
};

// ダウンサンプリング済みレンダーデータを保持するリングバッファ。
struct DownsampledRenderBuffer {
  const int size; // バッファ要素数（固定長）
  std::vector<float> buffer; // ダウンサンプル済みレンダーパワーのリング領域
  int write = 0; // 次に書き込むインデックス
  int read = 0; // 次に読み出すインデックス
    
  DownsampledRenderBuffer(size_t downsampled_buffer_size)
      : size(static_cast<int>(downsampled_buffer_size)),
        buffer(downsampled_buffer_size, 0.f) {
    std::fill(buffer.begin(), buffer.end(), 0.f);
  }

  int OffsetIndex(int index, int offset) const { return ::OffsetIndex(index, offset, size); }
  void UpdateWriteIndex(int offset) { write = OffsetIndex(write, offset); }
  void UpdateReadIndex(int offset) { read = OffsetIndex(read, offset); }
};


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
      const std::array<float, kFftLengthBy2Plus1>& spectrum = spectrum_buffer_->buffer[position];
      for (size_t k = 0; k < X2->size(); ++k) {
        (*X2)[k] += spectrum[k];
      }
      position = spectrum_buffer_->IncIndex(position);
    }
  }

  // スペクトルバッファへの参照を返す。
  const SpectrumBuffer& GetSpectrumBuffer() const { return *spectrum_buffer_; }

};
