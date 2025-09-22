// 残留エコーを抑圧する周波数帯ごとのゲインを計算する。
struct SuppressionGain {
  std::array<float, kFftLengthBy2Plus1> last_gain_; // 前回適用したゲイン
  std::array<float, kFftLengthBy2Plus1> last_nearend_; // 前回の近端スペクトル
  std::array<float, kFftLengthBy2Plus1> last_echo_; // 前回の残留エコースペクトル
  
  MovingAverage nearend_smoother_ {kFftLengthBy2Plus1, 4}; // 近端スペクトルを平滑化する移動平均

    
  // 固定パラメータ（全インスタンス共通）
  inline static constexpr float kMaxIncFactor = 2.0f; // ゲインの増加率上限
  inline static constexpr float kMaxDecFactorLf = 0.25f; // 低域でのゲイン減少率上限
    
  SuppressionGain()
      : last_nearend_(),
        last_echo_(),
        nearend_smoother_{kFftLengthBy2Plus1, 4} {
    last_gain_.fill(1.f);
  }


  static void ApplyLowFrequencyLimit(std::array<float, kFftLengthBy2Plus1>* gain) {
    (*gain)[0] = (*gain)[1] = std::min((*gain)[1], (*gain)[2]);
  }

  static void ApplyHighFrequencyLimit(std::array<float, kFftLengthBy2Plus1>* gain) {
    const size_t kFirstBandToLimit = (64 * 2000) / 8000;
    const float min_upper_gain = (*gain)[kFirstBandToLimit];
    for (size_t index = kFirstBandToLimit + 1; index < gain->size(); ++index) {
      (*gain)[index] = std::min((*gain)[index], min_upper_gain);
    }
    (*gain)[kFftLengthBy2] = (*gain)[kFftLengthBy2Minus1];
  }

  static void ApplyAudibilityWeight(float threshold,
                                    float normalizer,
                                    size_t begin,
                                    size_t end,
                                    std::span<const float> echo,
                                    std::span<float> weighted) {
    for (size_t index = begin; index < end; ++index) {
      if (echo[index] < threshold) {
        const float tmp = (threshold - echo[index]) * normalizer;
        weighted[index] = echo[index] * std::max(0.f, 1.f - tmp * tmp);
      } else {
        weighted[index] = echo[index];
      }
    }
  }

  static void WeightEchoForAudibilitySpan(std::span<const float> echo,
                                          std::span<float> weighted_echo) {
    const float kFloorPower = 2.f * 64.f;
    const float kAudibilityLf = 10.f;
    const float kAudibilityMf = 10.f;
    const float kAudibilityHf = 10.f;
    const float threshold_lf = kFloorPower * kAudibilityLf;
    const float normalizer_lf = 1.f / (threshold_lf - kFloorPower);
    ApplyAudibilityWeight(threshold_lf, normalizer_lf, 0, 3, echo, weighted_echo);
    const float threshold_mf = kFloorPower * kAudibilityMf;
    const float normalizer_mf = 1.f / (threshold_mf - kFloorPower);
    ApplyAudibilityWeight(threshold_mf, normalizer_mf, 3, 7, echo, weighted_echo);
    const float threshold_hf = kFloorPower * kAudibilityHf;
    const float normalizer_hf = 1.f / (threshold_hf - kFloorPower);
    ApplyAudibilityWeight(threshold_hf, normalizer_hf, 7, kFftLengthBy2Plus1, echo, weighted_echo);
  }

  // 近端・エコー・残留エコーのスペクトルからゲインを算出する。
  void GetGain(
      const std::array<float, kFftLengthBy2Plus1>& nearend_spectrum,
      const std::array<float, kFftLengthBy2Plus1>& echo_spectrum,
      const std::array<float, kFftLengthBy2Plus1>& residual_echo_spectrum,
      std::array<float, kFftLengthBy2Plus1>* low_band_gain) {
    // 近端判定は行わない。下位帯域のみを計算。
    LowerBandGain(nearend_spectrum, residual_echo_spectrum, low_band_gain);
  }

  // 可聴エコーが残らないようにゲインを制限する。
  void GainToNoAudibleEcho(const std::array<float, kFftLengthBy2Plus1>& nearend,
                           const std::array<float, kFftLengthBy2Plus1>& echo,
                           std::array<float, kFftLengthBy2Plus1>* gain) const {
    constexpr int kLastLfBand = 5;
    constexpr int kFirstHfBand = 8;
    constexpr float kLf_enr_transparent = 0.3f;
    constexpr float kLf_enr_suppress = 0.4f;
    constexpr float kLf_emr_transparent = 0.3f;
    constexpr float kHf_enr_transparent = 0.07f;
    constexpr float kHf_enr_suppress = 0.1f;
    constexpr float kHf_emr_transparent = 0.3f;
    for (size_t k = 0; k < gain->size(); ++k) {
      float a;
      if (static_cast<int>(k) <= kLastLfBand) {
        a = 0.f;
      } else if (static_cast<int>(k) < kFirstHfBand) {
        a = (static_cast<int>(k) - kLastLfBand) / static_cast<float>(kFirstHfBand - kLastLfBand);
      } else {
        a = 1.f;
      }
      const float enr_transparent = (1 - a) * kLf_enr_transparent + a * kHf_enr_transparent;
      const float enr_suppress = (1 - a) * kLf_enr_suppress + a * kHf_enr_suppress;
      const float emr_transparent = (1 - a) * kLf_emr_transparent + a * kHf_emr_transparent;
      const float enr = echo[k] / (nearend[k] + 1.f);
      const float emr = echo[k] / (1.f);
      float g = 1.0f;
      if (enr > enr_transparent && emr > emr_transparent) {
        g = (enr_suppress - enr) / (enr_suppress - enr_transparent);
        g = std::max(g, emr_transparent / emr);
      }
      (*gain)[k] = g;
    }
  }

  // 低域ゲインを算出し、近端スペクトル・残留エコーを踏まえて制限する。
  void LowerBandGain(
      const std::array<float, kFftLengthBy2Plus1>& suppressor_input,
      const std::array<float, kFftLengthBy2Plus1>& residual_echo,
      std::array<float, kFftLengthBy2Plus1>* gain) {
    gain->fill(1.f);
    std::array<float, kFftLengthBy2Plus1> max_gain;
    GetMaxGain(max_gain);
    std::array<float, kFftLengthBy2Plus1> G;
    std::array<float, kFftLengthBy2Plus1> nearend;
    nearend_smoother_.Average(suppressor_input, nearend);
    std::array<float, kFftLengthBy2Plus1> weighted_residual_echo;
    WeightEchoForAudibilitySpan(residual_echo, weighted_residual_echo);
    std::array<float, kFftLengthBy2Plus1> min_gain;
    GetMinGain(weighted_residual_echo, last_nearend_, last_echo_, min_gain);
    GainToNoAudibleEcho(nearend, weighted_residual_echo, &G);
    for (size_t k = 0; k < gain->size(); ++k) {
      G[k] = std::max(std::min(G[k], max_gain[k]), min_gain[k]);
      (*gain)[k] = std::min((*gain)[k], G[k]);
    }
    std::copy(nearend.begin(), nearend.end(), last_nearend_.begin());
    std::copy(weighted_residual_echo.begin(), weighted_residual_echo.end(),
              last_echo_.begin());
    ApplyLowFrequencyLimit(gain);
    ApplyHighFrequencyLimit(gain);
    std::copy(gain->begin(), gain->end(), last_gain_.begin());
    for (size_t i = 0; i < kFftLengthBy2Plus1; ++i) {
      float v = (*gain)[i];
      if (v < 0.f) v = 0.f;
      (*gain)[i] = std::sqrt(v);
    }
  }

  // 最低限許容するゲイン値を算出する。
  void GetMinGain(std::span<const float> weighted_residual_echo,
                  std::span<const float> last_nearend,
                  std::span<const float> last_echo,
                  std::span<float> min_gain) const {
    const float min_echo_power = 64.f;
    for (size_t k = 0; k < min_gain.size(); ++k) {
      min_gain[k] = weighted_residual_echo[k] > 0.f
                        ? min_echo_power / weighted_residual_echo[k]
                        : 1.f;
      min_gain[k] = std::min(min_gain[k], 1.f);
    }
    const float dec = kMaxDecFactorLf;
    const int kLastLfSmoothingBand = 5;
    const int kLastPermanentLfSmoothingBand = 0;
    for (int k = 0; k <= kLastLfSmoothingBand; ++k) {
      if (last_nearend[k] > last_echo[k] ||
          k <= kLastPermanentLfSmoothingBand) {
        min_gain[k] = std::max(min_gain[k], last_gain_[k] * dec);
        min_gain[k] = std::min(min_gain[k], 1.f);
      }
    }
  }

  void GetMaxGain(std::span<float> max_gain) const {
    const float inc = kMaxIncFactor;
    const float floor = 0.00001f;
    for (size_t k = 0; k < max_gain.size(); ++k) {
      max_gain[k] = std::min(std::max(last_gain_[k] * inc, floor), 1.f);
    }
  }


};

 
