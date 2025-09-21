#include <stddef.h>
#include <span>
#include <array>
 

// Estimates the delay of the echo path.
struct EchoPathDelayEstimator {
  EchoPathDelayEstimator()
      : sub_block_size_(kBlockSize / kDownSamplingFactor),
        matched_filter_(),
        matched_filter_lag_aggregator_(MatchedFilter::kMaxFilterLag) {}
  


  // Resets the estimation. If the delay confidence is reset, the reset behavior
  // is as if the call is restarted.
  void Reset() { ResetInternal(); }

  // Produce a delay estimate in samples, or -1 if not available.
  int EstimateDelay(
      const DownsampledRenderBuffer& render_buffer,
      const Block& capture) {
    std::array<float, kBlockSize> downsampled_capture_data;
    std::span<float> downsampled_capture(downsampled_capture_data.data(),
                                         sub_block_size_);
    DecimateBy4(capture.View(), downsampled_capture);
    matched_filter_.Update(render_buffer, downsampled_capture);

    int aggregated_matched_filter_lag =
        matched_filter_lag_aggregator_.Aggregate(
            matched_filter_.GetBestLagEstimate());

    if (aggregated_matched_filter_lag >= 0) {
      aggregated_matched_filter_lag *= static_cast<int>(kDownSamplingFactor);
    }

    if (old_aggregated_lag_ >= 0 && aggregated_matched_filter_lag >= 0 &&
        old_aggregated_lag_ == aggregated_matched_filter_lag) {
      ++consistent_estimate_counter_;
    } else {
      consistent_estimate_counter_ = 0;
    }
    old_aggregated_lag_ = aggregated_matched_filter_lag;
    const size_t kNumBlocksPerSecondBy2 = kNumBlocksPerSecond / 2;
    if (consistent_estimate_counter_ > kNumBlocksPerSecondBy2) {
      ResetInternal();
    }
    return aggregated_matched_filter_lag;
  }

  

  inline static constexpr size_t kDownSamplingFactor = 4;
  const size_t sub_block_size_;
  // Mono: no downmix required.
  MatchedFilter matched_filter_;
  MatchedFilterLagAggregator matched_filter_lag_aggregator_;
  int old_aggregated_lag_ = -1;
  size_t consistent_estimate_counter_ = 0;
  

  // Internal reset method with more granularity.
  void ResetInternal() {
    matched_filter_lag_aggregator_.Reset(true);
    matched_filter_.Reset();
    old_aggregated_lag_ = -1;
    consistent_estimate_counter_ = 0;
  }
};
 


