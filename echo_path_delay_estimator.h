// エコーパスの遅延を推定する。
struct EchoPathDelayEstimator {
  inline static constexpr size_t kDownSamplingFactor = 4; // 遅延推定で用いるダウンサンプリング倍率
  const size_t sub_block_size_; // ダウンサンプリング後のサブブロック長
  MatchedFilter matched_filter_; // 遅延候補を算出するマッチドフィルタ
  MatchedFilterLagAggregator matched_filter_lag_aggregator_; // マッチドフィルタの遅延推定値を平滑化する集約器
  int old_aggregated_lag_ = -1; // 直前に確定した遅延サンプル値
  size_t consistent_estimate_counter_ = 0; // 同一推定が続いた回数
  
    
  EchoPathDelayEstimator()
      : sub_block_size_(kBlockSize / kDownSamplingFactor),
        matched_filter_(),
        matched_filter_lag_aggregator_(MatchedFilter::kMaxFilterLag) {}


  // 推定状態を初期化する。遅延信頼度もリセットして再起動直後と同等に戻す。
  void Reset() { ResetInternal(); }

  // 遅延サンプル数を推定し、得られなければ -1 を返す。
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
  // 内部状態をまとめてリセットする補助関数。
  void ResetInternal() {
    matched_filter_lag_aggregator_.Reset(true);
    matched_filter_.Reset();
    old_aggregated_lag_ = -1;
    consistent_estimate_counter_ = 0;
  }
};
 

