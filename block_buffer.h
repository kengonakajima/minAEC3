#include <stddef.h>
#include <vector>
 

// Struct for bundling a circular buffer of two dimensional vector objects
// together with the read and write indices.
struct BlockBuffer {
  BlockBuffer(size_t size)
      : size(static_cast<int>(size)), buffer(size, Block()) {}
  ~BlockBuffer() = default;

  int IncIndex(int index) const { return ::IncIndex(index, size); }

  int DecIndex(int index) const { return ::DecIndex(index, size); }

  int OffsetIndex(int index, int offset) const {
    return ::OffsetIndex(index, offset, size);
  }

  void UpdateWriteIndex(int offset) { write = OffsetIndex(write, offset); }
  void IncWriteIndex() { write = IncIndex(write); }
  void DecWriteIndex() { write = DecIndex(write); }
  void UpdateReadIndex(int offset) { read = OffsetIndex(read, offset); }
  void IncReadIndex() { read = IncIndex(read); }
  void DecReadIndex() { read = DecIndex(read); }

  const int size;
  std::vector<Block> buffer;
  int write = 0;
  int read = 0;
};

