#include <vector>
#include <array>
#include <algorithm>
#include <span>
 

// Aggregates lag estimates produced by the MatchedFilter class into a single
// reliable combined lag estimate.
struct MatchedFilterLagAggregator {
  explicit MatchedFilterLagAggregator(size_t max_filter_lag)
      : thresholds_(),
        headroom_(static_cast<int>(32 / 4)),  // 64/4=16 samples -> headroom 8 blocks? simplified
        highest_peak_aggregator_(max_filter_lag) {}


  // Resets the aggregator.
  void Reset(bool hard_reset) {
    highest_peak_aggregator_.Reset();
    if (hard_reset) {
      significant_candidate_found_ = false;
    }
  }

  // Aggregates the provided lag estimates. Returns delay in samples, or -1 if none.
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

  // 最高ピークや信頼フラグの外部公開は削除。

  struct HighestPeakAggregator {
    explicit HighestPeakAggregator(size_t max_filter_lag)
        : histogram_(max_filter_lag + 1, 0) {
      histogram_data_.fill(0);
    }
    void Reset() {
      std::fill(histogram_.begin(), histogram_.end(), 0);
      histogram_data_.fill(0);
      histogram_data_index_ = 0;
    }
    void Aggregate(int lag) {
      --histogram_[histogram_data_[histogram_data_index_]];
      histogram_data_[histogram_data_index_] = lag;
      ++histogram_[histogram_data_[histogram_data_index_]];
      histogram_data_index_ = (histogram_data_index_ + 1) % histogram_data_.size();
      candidate_ = std::distance(
          histogram_.begin(),
          std::max_element(histogram_.begin(), histogram_.end()));
    }
    int candidate() const { return candidate_; }
    std::span<const int> histogram() const { return histogram_; }

   
    std::vector<int> histogram_;
    std::array<int, 250> histogram_data_;
    int histogram_data_index_ = 0;
    int candidate_ = -1;
  };

  bool significant_candidate_found_ = false;
  struct Thresholds { int initial; int converged; };
  const Thresholds thresholds_ {5, 20};
  const int headroom_;
  HighestPeakAggregator highest_peak_aggregator_;
};
 

