// FftData のリングバッファと読み書きインデックスをまとめた構造体。
struct FftBuffer {
  const int size; // バッファ長（保持するFftDataの数）
  std::vector<FftData> buffer; // FftDataのリングバッファ
  int write = 0; // 次に書き込む位置
  int read = 0; // 次に読み出す位置
    
  FftBuffer(size_t size) : size(static_cast<int>(size)), buffer(size) {
    for (auto& fft_data : buffer) fft_data.Clear();
  }

  int IncIndex(int index) const { return ::IncIndex(index, size); }
  int DecIndex(int index) const { return ::DecIndex(index, size); }
  int OffsetIndex(int index, int offset) const { return ::OffsetIndex(index, offset, size); }
  void UpdateWriteIndex(int offset) { write = OffsetIndex(write, offset); }
  void IncWriteIndex() { write = IncIndex(write); }
  void DecWriteIndex() { write = DecIndex(write); }
  void UpdateReadIndex(int offset) { read = OffsetIndex(read, offset); }
  void IncReadIndex() { read = IncIndex(read); }
  void DecReadIndex() { read = DecIndex(read); }
};

 
