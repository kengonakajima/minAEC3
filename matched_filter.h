
 

// Produces recursively updated cross-correlation estimates for several signal
// shifts where the intra-shift spacing is uniform.
struct MatchedFilter {
  static inline constexpr float kSmoothing = 0.7f; // TODO
  static inline constexpr float kMatchingFilterThreshold = 0.2f; // TODO
  static inline constexpr float kExcitationLimit = 150.f; // TODO
  // Stores properties for the lag estimate corresponding to a particular signal shift.
  struct LagEstimate {
    LagEstimate() = default;
    LagEstimate(size_t lag) : lag(lag) {}
    size_t lag = 0;
  };
  
  static constexpr size_t kNumFilters = 5; // TODO
  static constexpr size_t kSubBlockSize = kBlockSize / 4; // TODO
  static constexpr size_t kFilterLength = kMatchedFilterWindowSizeSubBlocks * kSubBlockSize; // TODO
  static constexpr size_t kFilterIntraLagShift = kMatchedFilterAlignmentShiftSizeSubBlocks * kSubBlockSize; // TODO
  static constexpr size_t kMaxFilterLag = kNumFilters * kFilterIntraLagShift + kFilterLength; // TODO

  std::vector<std::vector<float>> filters_; // TODO
  int reported_lag_ = -1; // TODO
  int winner_lag_ = -1; // TODO
    
  MatchedFilter() : filters_( /*num_filters=*/kNumFilters, std::vector<float>(kFilterLength, 0.f)) {}
  

  // Updates the correlation with the values in the capture buffer.
  void Update(const DownsampledRenderBuffer& render_buffer,
              std::span<const float> capture) {

    auto MaxSquarePeakIndex = [](std::span<const float> h) -> size_t {
      if (h.size() < 2) {
        return 0;
      }
      float max_element1 = h[0] * h[0];
      float max_element2 = h[1] * h[1];
      size_t lag_estimate1 = 0;
      size_t lag_estimate2 = 1;
      const size_t last_index = h.size() - 1;
      for (size_t k = 2; k < last_index; k += 2) {
        float element1 = h[k] * h[k];
        float element2 = h[k + 1] * h[k + 1];
        if (element1 > max_element1) {
          max_element1 = element1;
          lag_estimate1 = k;
        }
        if (element2 > max_element2) {
          max_element2 = element2;
          lag_estimate2 = k + 1;
        }
      }
      if (max_element2 > max_element1) {
        max_element1 = max_element2;
        lag_estimate1 = lag_estimate2;
      }
      float last_element = h[last_index] * h[last_index];
      if (last_element > max_element1) {
        return last_index;
      }
      return lag_estimate1;
    };

    auto MatchedFilterCore = [&](size_t x_start_index,
                                 float x2_sum_threshold,
                                 std::span<const float> x,
                                 std::span<const float> y,
                                 std::span<float> h,
                                 bool* filters_updated,
                                 float* error_sum) {
      for (size_t i = 0; i < y.size(); ++i) {
        float x2_sum = 0.f;
        float s = 0;
        size_t x_index = x_start_index;
        for (size_t k = 0; k < h.size(); ++k) {
          x2_sum += x[x_index] * x[x_index];
          s += h[k] * x[x_index];
          x_index = x_index < (x.size() - 1) ? x_index + 1 : 0;
        }

        float e = y[i] - s;
        (*error_sum) += e * e;

        if (x2_sum > x2_sum_threshold) {
          const float alpha = kSmoothing * e / x2_sum;
          size_t x_index = x_start_index;
          for (size_t k = 0; k < h.size(); ++k) {
            h[k] += alpha * x[x_index];
            x_index = x_index < (x.size() - 1) ? x_index + 1 : 0;
          }
          *filters_updated = true;
        }

        x_start_index = x_start_index > 0 ? x_start_index - 1 : x.size() - 1;
      }
    };

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

  // Resets the matched filter.
  void Reset() {
    for (auto& f : filters_) {
      std::fill(f.begin(), f.end(), 0.f);
    }
    winner_lag_ = -1;
    reported_lag_ = -1;
  }

  // Returns the current lag estimates.
  int GetBestLagEstimate() const { return reported_lag_; }



};

