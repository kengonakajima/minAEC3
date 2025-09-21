// 直近期の遅延推定値をヒストグラムで管理し、最も多く出現した遅延を候補とする集約器。
struct HighestPeakAggregator {
    std::vector<int> histogram_; // 遅延ごとの出現回数を保持するヒストグラム
    std::array<int, 250> histogram_data_; // 最近の遅延推定値をFIFOで保持するバッファ
    int histogram_data_index_ = 0; 
    int candidate_ = -1; // 現在最も出現頻度が高い遅延候補

    HighestPeakAggregator(size_t max_filter_lag)
        : histogram_(max_filter_lag + 1, 0) {
        histogram_data_.fill(0);
    }
    // 状態をリセットする。
    void Reset() {
        std::fill(histogram_.begin(), histogram_.end(), 0);
        histogram_data_.fill(0);
        histogram_data_index_ = 0;
        candidate_ = -1;
    }
    // 遅延値をヒストグラムに追加し、候補を更新する。
    void Aggregate(int lag) {
        --histogram_[histogram_data_[histogram_data_index_]];
        histogram_data_[histogram_data_index_] = lag;
        ++histogram_[histogram_data_[histogram_data_index_]];
        histogram_data_index_ = (histogram_data_index_ + 1) % histogram_data_.size();
        candidate_ = std::distance(
                                   histogram_.begin(),
                                   std::max_element(histogram_.begin(), histogram_.end()));
    }
    // 現在の候補遅延を返す。
    int candidate() const { return candidate_; }
    // ヒストグラム内容を参照する。
    std::span<const int> histogram() const { return histogram_; }
};
// マッチドフィルタが出す遅延推定を集約し、信頼できる値を選び出す。
struct MatchedFilterLagAggregator {
  bool significant_candidate_found_ = false; // 収束と見なせる候補が見つかったか
  struct Thresholds { int initial; int converged; }; // 遅延確定のための閾値群
  const Thresholds thresholds_ {5, 20}; // 初期判定と収束判定の閾値
  const int headroom_; // レンダー遅延の先行マージン（サンプル数）
  HighestPeakAggregator highest_peak_aggregator_; // ヒストグラム集約器
    
  MatchedFilterLagAggregator(size_t max_filter_lag)
      : thresholds_(),
        headroom_(static_cast<int>(32 / 4)),  // 64サンプル/4=16サンプル → マージンを簡略化して設定
        highest_peak_aggregator_(max_filter_lag) {}


  // 集約器の状態をリセットする。
  void Reset(bool hard_reset) {
    highest_peak_aggregator_.Reset();
    if (hard_reset) {
      significant_candidate_found_ = false;
    }
  }

  // 遅延推定値を集約し、確信度が十分ならサンプル単位の遅延を返す（なければ -1）。
  int Aggregate(int lag_estimate) {
    if (lag_estimate >= 0) {
      highest_peak_aggregator_.Aggregate(std::max(0, lag_estimate - headroom_));
      std::span<const int> histogram = highest_peak_aggregator_.histogram();
      int candidate = highest_peak_aggregator_.candidate();
      significant_candidate_found_ = significant_candidate_found_ ||
                                     histogram[candidate] > thresholds_.converged;
      if (histogram[candidate] > thresholds_.converged ||
          (histogram[candidate] > thresholds_.initial &&
           !significant_candidate_found_)) {
        return candidate;
      }
    }
    return -1;
  }

};
 
