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


