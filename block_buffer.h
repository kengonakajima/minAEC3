// 2次元ベクトル（ここではBlock）を保持するリングバッファと読み書きインデックスをまとめた構造体。
struct BlockBuffer {
  const int size; // バッファ内のBlock数（固定長）
  std::vector<Block> buffer; // 実データを保持するリングバッファ
  int write = 0; // 次に書き込む位置
  int read = 0; // 次に読み出す位置
    
  BlockBuffer(size_t size) : size(static_cast<int>(size)), buffer(size, Block()) {}
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
