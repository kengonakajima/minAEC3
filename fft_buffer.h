// Struct for bundling a circular buffer of FftData objects together with the
// read and write indices.
struct FftBuffer {
  const int size; // TODO
  std::vector<FftData> buffer; // TODO
  int write = 0; // TODO
  int read = 0; // TODO
    
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

 

