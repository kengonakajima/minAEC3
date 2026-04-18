
// 係数4での単純なダウンサンプル（平均化）。
// in: 元のフレーム（例: 64サンプル）
// out: ダウンサンプル先（例: 16サンプル）
inline void DecimateBy4(std::span<const float> in, std::span<float> out) {
  const size_t f = 4;
  for (size_t j = 0; j < out.size(); ++j) {
    const size_t k0 = j * f;
    float acc = 0.f;
    size_t cnt = 0;
    for (size_t t = 0; t < f && (k0 + t) < in.size(); ++t) {
      acc += in[k0 + t];
      ++cnt;
    }
    out[j] = cnt ? (acc / static_cast<float>(cnt)) : 0.f;
  }
}


// レンダーブロックを遅延付きで保持し、指定遅延で取り出せるようにする。
struct RenderDelayBuffer {
  inline static constexpr size_t kDownSamplingFactor = 4; // 遅延推定で用いるダウンサンプリング倍率
  const int sub_block_size_; // ダウンサンプル後のサブブロック長
  BlockBuffer blocks_; // レンダーブロックのリングバッファ
  SpectrumBuffer spectra_; // レンダースペクトルのリングバッファ
  FftBuffer ffts_; // FFT済みレンダーデータのリングバッファ
  int delay_; // 現在適用中の遅延（ブロック単位）
  RenderBuffer echo_remover_buffer_; // EchoRemoverへ渡すバッファビュー
  DownsampledRenderBuffer low_rate_; // ダウンサンプリング済みレンダーデータ
  std::vector<float> render_ds_; // ダウンサンプル用ワーク領域
  const int buffer_headroom_; // バッファの安全余裕ブロック数
    
  RenderDelayBuffer()
      : sub_block_size_(static_cast<int>(kBlockSize / kDownSamplingFactor)),
        blocks_(GetRenderDelayBufferSize(kDownSamplingFactor,
                                         /*num_filters=*/5,
                                         /*filter_length_blocks=*/13)),
        spectra_(blocks_.buffer.size()),
        ffts_(blocks_.buffer.size()),
        delay_(-1),
        echo_remover_buffer_(&blocks_, &spectra_, &ffts_),
        low_rate_(GetDownSampledBufferSize(kDownSamplingFactor,
                                           /*num_filters=*/5)),
        render_ds_(sub_block_size_, 0.f),
        buffer_headroom_(13) {
    Reset();
  }
  

  // バッファの整列状態をリセットする。
  void Reset() {
    low_rate_.read = low_rate_.OffsetIndex(low_rate_.write, sub_block_size_);
    ApplyTotalDelay(/*default_delay_blocks=*/10);
    delay_ = -1;
  }

  // レンダーブロックをバッファへ挿入する。
  void Insert(const Block& block) {
    const int previous_write = blocks_.write;
    IncrementWriteIndices();
    InsertBlock(block, previous_write);
  }

  // バッファを1ステップ進める。
  void PrepareCaptureProcessing() {
    IncrementLowRateReadIndices();
    IncrementReadIndices();
  }


  // 遅延量を設定し、変更があったかどうかを返す。
  bool AlignFromDelay(size_t delay) {
    if (delay_ == static_cast<int>(delay)) {
      return false;
    }
    delay_ = static_cast<int>(delay);
    // ダウンサンプル済みバッファの現在レイテンシ(ブロック数)を算出し、外部推定遅延に加算する。
    const int latency_samples =
        (low_rate_.buffer.size() + low_rate_.read - low_rate_.write) %
        low_rate_.buffer.size();
    const int latency_blocks = latency_samples / sub_block_size_;
    int total_delay = latency_blocks + delay_;
    total_delay = static_cast<int>(std::min(MaxDelay(), static_cast<size_t>(std::max(total_delay, 0))));
    ApplyTotalDelay(total_delay);
    return true;
  }


  // 適用可能な最大遅延を返す。
  size_t MaxDelay() const { return blocks_.buffer.size() - 1 - buffer_headroom_; }

  // EchoRemover用レンダーバッファを取得する。
  RenderBuffer* GetRenderBuffer() { return &echo_remover_buffer_; }

  // ダウンサンプリング済みレンダーバッファを取得する。
  const DownsampledRenderBuffer& GetDownsampledRenderBuffer() const { return low_rate_; }
  void ApplyTotalDelay(int delay) {
    blocks_.read = blocks_.OffsetIndex(blocks_.write, -delay);
    spectra_.read = spectra_.OffsetIndex(spectra_.write, delay);
    ffts_.read = ffts_.OffsetIndex(ffts_.write, delay);
  }
  // ブロックを挿入して各種バッファを更新する。
  // block: 追加するレンダーブロック, previous_write: 更新前のwrite位置
  void InsertBlock(const Block& block, int previous_write) {
    BlockBuffer& b = blocks_;
    DownsampledRenderBuffer& lr = low_rate_;
    std::vector<float>& ds = render_ds_;
    FftBuffer& f = ffts_;
    SpectrumBuffer& s = spectra_;
    std::copy(block.begin(), block.end(), b.buffer[b.write].begin());
    DecimateBy4(b.buffer[b.write], ds);
    std::copy(ds.rbegin(), ds.rend(), lr.buffer.begin() + lr.write);
    PaddedFft(b.buffer[b.write],
              b.buffer[previous_write],
              &f.buffer[f.write]);
    f.buffer[f.write].Spectrum(s.buffer[s.write]);
  }
  void IncrementWriteIndices() {
    low_rate_.UpdateWriteIndex(-sub_block_size_);
    blocks_.IncWriteIndex();
    spectra_.DecWriteIndex();
    ffts_.DecWriteIndex();
  }
  void IncrementLowRateReadIndices() { low_rate_.UpdateReadIndex(-sub_block_size_); }
  void IncrementReadIndices() {
    if (blocks_.read != blocks_.write) {
      blocks_.IncReadIndex();
      spectra_.DecReadIndex();
      ffts_.DecReadIndex();
    }
  }
};

 

// 複数の信号シフトに対する相互相関を逐次更新し、遅延候補を推定する。
struct MatchedFilter {
  static inline constexpr float kSmoothing = 0.7f; // フィルタ更新時の平滑係数
  static inline constexpr float kMatchingFilterThreshold = 0.2f; // 適合度判断の閾値
  static inline constexpr float kExcitationLimit = 150.f; // 励起不足を防ぐ入力パワーの下限

  static constexpr size_t kNumFilters = 5; // 追跡する遅延候補数
  static constexpr size_t kSubBlockSize = kBlockSize / 4; // ダウンサンプル後サブブロック長
  static constexpr size_t kFilterLength = kMatchedFilterWindowSizeSubBlocks * kSubBlockSize; // 各フィルタ長
  static constexpr size_t kFilterIntraLagShift = kMatchedFilterAlignmentShiftSizeSubBlocks * kSubBlockSize; // フィルタ間の遅延シフト量
  static constexpr size_t kMaxFilterLag = kNumFilters * kFilterIntraLagShift + kFilterLength; // 推定する最大遅延

  std::vector<std::vector<float>> filters_; // 遅延候補ごとの適応フィルタ係数
  int reported_lag_ = -1; // 現在報告している遅延
  int winner_lag_ = -1; // 直近日に最も信頼できた遅延
    
  MatchedFilter() : filters_( /*num_filters=*/kNumFilters, std::vector<float>(kFilterLength, 0.f)) {}
  
  static size_t MaxSquarePeakIndex(std::span<const float> coefficients) {
    if (coefficients.size() < 2) {
      return 0;
    }
    float max_element1 = coefficients[0] * coefficients[0];
    float max_element2 = coefficients[1] * coefficients[1];
    size_t lag_estimate1 = 0;
    size_t lag_estimate2 = 1;
    const size_t last_index = coefficients.size() - 1;
    for (size_t index = 2; index < last_index; index += 2) {
      const float element1 = coefficients[index] * coefficients[index];
      const float element2 = coefficients[index + 1] * coefficients[index + 1];
      if (element1 > max_element1) {
        max_element1 = element1;
        lag_estimate1 = index;
      }
      if (element2 > max_element2) {
        max_element2 = element2;
        lag_estimate2 = index + 1;
      }
    }
    if (max_element2 > max_element1) {
      max_element1 = max_element2;
      lag_estimate1 = lag_estimate2;
    }
    const float last_element = coefficients[last_index] * coefficients[last_index];
    if (last_element > max_element1) {
      return last_index;
    }
    return lag_estimate1;
  }

  void MatchedFilterCore(size_t x_start_index,
                         float x2_sum_threshold,
                         std::span<const float> x,
                         std::span<const float> y,
                         std::span<float> h,
                         bool* filters_updated,
                         float* error_sum) const {
    for (size_t i = 0; i < y.size(); ++i) {
      float x2_sum = 0.f;
      float s = 0.f;
      size_t x_index = x_start_index;
      for (size_t k = 0; k < h.size(); ++k) {
        x2_sum += x[x_index] * x[x_index];
        s += h[k] * x[x_index];
        x_index = x_index < (x.size() - 1) ? x_index + 1 : 0;
      }

      const float e = y[i] - s;
      (*error_sum) += e * e;

      if (x2_sum > x2_sum_threshold) {
        const float alpha = kSmoothing * e / x2_sum;
        size_t adapt_index = x_start_index;
        for (size_t k = 0; k < h.size(); ++k) {
          h[k] += alpha * x[adapt_index];
          adapt_index = adapt_index < (x.size() - 1) ? adapt_index + 1 : 0;
        }
        *filters_updated = true;
      }

      x_start_index = x_start_index > 0 ? x_start_index - 1 : x.size() - 1;
    }
  }


  // キャプチャバッファを使って相互相関を更新する。
  void Update(const DownsampledRenderBuffer& render_buffer,
              std::span<const float> capture) {

    const float x2_sum_threshold =
        filters_[0].size() * kExcitationLimit * kExcitationLimit;

    float error_sum_anchor = 0.0f;
    for (size_t k = 0; k < capture.size(); ++k) {
      error_sum_anchor += capture[k] * capture[k];
    }

    float winner_error_sum = error_sum_anchor;
    winner_lag_ = -1;
    reported_lag_ = -1;
    size_t alignment_shift = 0;
    int previous_lag_estimate = -1;
    const int num_filters = static_cast<int>(filters_.size());

    int winner_index = -1;
    for (int n = 0; n < num_filters; ++n) {
      float error_sum = 0.f;
      bool filters_updated = false;
      size_t x_start_index =
          (render_buffer.read + alignment_shift + kSubBlockSize - 1) %
          render_buffer.buffer.size();

      MatchedFilterCore(x_start_index, x2_sum_threshold,
                        render_buffer.buffer, capture, filters_[n],
                        &filters_updated, &error_sum);

      const size_t lag_estimate = MaxSquarePeakIndex(filters_[n]);
      const bool reliable =
          lag_estimate > 2 && lag_estimate < (filters_[n].size() - 10) &&
          error_sum < kMatchingFilterThreshold * error_sum_anchor;
      const int lag = static_cast<int>(lag_estimate + alignment_shift);

      if (filters_updated && reliable && error_sum < winner_error_sum) {
        winner_error_sum = error_sum;
        winner_index = n;
        if (previous_lag_estimate >= 0 && previous_lag_estimate == lag) {
          winner_lag_ = previous_lag_estimate;
          winner_index = n - 1;
        } else {
          winner_lag_ = lag;
        }
      }

      previous_lag_estimate = lag;
      alignment_shift += kFilterIntraLagShift;
    }

    if (winner_index != -1) {
      reported_lag_ = winner_lag_;
    }
  }

  // マッチドフィルタの状態をリセットする。
  void Reset() {
    for (std::vector<float>& filter : filters_) {
      std::fill(filter.begin(), filter.end(), 0.f);
    }
    winner_lag_ = -1;
    reported_lag_ = -1;
  }

  // 現在の遅延推定値を返す。
  int GetBestLagEstimate() const { return reported_lag_; }



};
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
  static constexpr int kInitialThreshold = 5;   // 未収束時に候補確定に必要な出現回数
  static constexpr int kConvergedThreshold = 20; // 収束済み判定に必要な出現回数
  bool significant_candidate_found_ = false; // 収束と見なせる候補が見つかったか
  const int headroom_; // レンダー遅延の先行マージン（サンプル数）
  HighestPeakAggregator highest_peak_aggregator_; // ヒストグラム集約器

  MatchedFilterLagAggregator(size_t max_filter_lag)
      : headroom_(static_cast<int>(32 / 4)),  // 64サンプル/4=16サンプル → マージンを簡略化して設定
        highest_peak_aggregator_(max_filter_lag) {}


  // 集約器の状態をリセットする。
  void Reset() {
    highest_peak_aggregator_.Reset();
    significant_candidate_found_ = false;
  }

  // 遅延推定値を集約し、確信度が十分ならサンプル単位の遅延を返す（なければ -1）。
  int Aggregate(int lag_estimate) {
    if (lag_estimate >= 0) {
      highest_peak_aggregator_.Aggregate(std::max(0, lag_estimate - headroom_));
      std::span<const int> histogram = highest_peak_aggregator_.histogram();
      int candidate = highest_peak_aggregator_.candidate();
      significant_candidate_found_ = significant_candidate_found_ ||
                                     histogram[candidate] > kConvergedThreshold;
      if (histogram[candidate] > kConvergedThreshold ||
          (histogram[candidate] > kInitialThreshold &&
           !significant_candidate_found_)) {
        return candidate;
      }
    }
    return -1;
  }

};
 
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
    DecimateBy4(capture, downsampled_capture);
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
    matched_filter_lag_aggregator_.Reset();
    matched_filter_.Reset();
    old_aggregated_lag_ = -1;
    consistent_estimate_counter_ = 0;
  }
};
 

