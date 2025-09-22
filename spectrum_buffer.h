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
  int DecIndex(int index) const { return ::DecIndex(index, size); }
  int OffsetIndex(int index, int offset) const { return ::OffsetIndex(index, offset, size); }
  void UpdateWriteIndex(int offset) { write = OffsetIndex(write, offset); }
  void IncWriteIndex() { write = IncIndex(write); }
  void DecWriteIndex() { write = DecIndex(write); }
  void UpdateReadIndex(int offset) { read = OffsetIndex(read, offset); }
  void IncReadIndex() { read = IncIndex(read); }
  void DecReadIndex() { read = DecIndex(read); }
};

