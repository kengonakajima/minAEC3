// 逐次入力に対して移動平均を求めるユーティリティ。
struct MovingAverage {
  const size_t num_elem_; // 1入力あたりの要素数（並列に平均する系列数）
  const size_t mem_len_; // 過去入力を保持するスロット数（平均区間長-1）
  const float scaling_; // 平均を取る際のスケーリング係数（1 / 平均区間長）
  std::vector<float> memory_; // 過去入力を直列に格納するワーク領域
  size_t mem_index_; // memory_ 内で次に書き込むスロット位置
    
  // 要素数 num_elem の入力を受け取り、mem_len 個のデータで平均するインスタンスを生成。
  MovingAverage(size_t num_elem, size_t mem_len)
      : num_elem_(num_elem),
        mem_len_(mem_len - 1),
        scaling_(1.0f / static_cast<float>(mem_len)),
        memory_(num_elem * mem_len_, 0.f),
        mem_index_(0) {}
  

  // 現在の入力と過去 mem_len_-1 個の入力を平均して output に書き込む。
  void Average(std::span<const float> input, std::span<float> output) {
    std::copy(input.begin(), input.end(), output.begin());
    for (std::vector<float>::iterator i = memory_.begin(); i < memory_.end(); i += num_elem_) {
      std::transform(i, i + num_elem_, output.begin(), output.begin(), std::plus<float>());
    }
    for (float& o : output) {
      o *= scaling_;
    }
    if (mem_len_ > 0) {
      std::copy(input.begin(), input.end(),
                memory_.begin() + mem_index_ * num_elem_);
      mem_index_ = (mem_index_ + 1) % mem_len_;
    }
  }
};
