// 4ミリ秒分のモノラル音声データを保持する。
// 単一バンドでサンプリング周波数は16 kHz固定。
struct Block {
  std::array<float, kBlockSize> data_; // 固定長サンプル列（64サンプル=4ms）
    
  Block() : data_{} {
    data_.fill(0.0f);
  }

  // データへアクセスするためのイテレータ。
  std::array<float, kBlockSize>::iterator begin() { return data_.begin(); }
  std::array<float, kBlockSize>::const_iterator begin() const { return data_.begin(); }
  std::array<float, kBlockSize>::iterator end() { return data_.end(); }
  std::array<float, kBlockSize>::const_iterator end() const { return data_.end(); }

  // std::span経由でデータ全体を参照する。
  std::span<float, kBlockSize> View() {
    return std::span<float, kBlockSize>(data_.data(), kBlockSize);
  }

  std::span<const float, kBlockSize> View() const {
    return std::span<const float, kBlockSize>(data_.data(), kBlockSize);
  }
};

 
