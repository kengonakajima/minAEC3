
// Contains 4 milliseconds of mono audio data.
// Single-band, sampling rate 16 kHz 固定。
struct Block {
  Block(float default_value = 0.0f)
      : data_{} {
    data_.fill(default_value);
  }

  // Iterators for accessing the data.
  auto begin() { return data_.begin(); }

  auto begin() const { return data_.begin(); }

  auto end() { return data_.begin() + kBlockSize; }

  auto end() const { return data_.begin() + kBlockSize; }

  // Access data via std::span.
  std::span<float, kBlockSize> View() {
    return std::span<float, kBlockSize>(data_.data(), kBlockSize);
  }

  std::span<const float, kBlockSize> View() const {
    return std::span<const float, kBlockSize>(data_.data(), kBlockSize);
  }

  std::array<float, kBlockSize> data_;
};

 

