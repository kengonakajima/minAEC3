
struct ResidualEchoEstimator {
  ResidualEchoEstimator() = default;

  static void ComputeEchoGeneratingPower(const SpectrumBuffer& spectrum_buffer,
                                         int filter_delay_blocks,
                                         std::span<float, kFftLengthBy2Plus1> X2) {
    // 遅延推定位置の前後 ±1 ブロックをまとめて解析窓とする。
    const size_t window_start = std::max(0, filter_delay_blocks - 1);
    const size_t window_end = filter_delay_blocks + 1;
    const int idx_start = spectrum_buffer.OffsetIndex(spectrum_buffer.read, window_start);
    const int idx_stop = spectrum_buffer.OffsetIndex(spectrum_buffer.read, window_end + 1);
    std::fill(X2.begin(), X2.end(), 0.f);
    for (int index = idx_start; index != idx_stop; index = spectrum_buffer.IncIndex(index)) {
      for (size_t bin = 0; bin < kFftLengthBy2Plus1; ++bin) {
        X2[bin] = std::max(X2[bin], spectrum_buffer.buffer[index][bin]);
      }
    }
  }


  // 残留エコーのパワースペクトルR2を推定する。
  void Estimate(const AecState& aec_state,
                const RenderBuffer& render_buffer,
                const std::array<float, kFftLengthBy2Plus1>& S2_linear,
                const std::array<float, kFftLengthBy2Plus1>& Y2,
                std::array<float, kFftLengthBy2Plus1>* R2) {
    if (aec_state.UsableLinearEstimate()) {
      // Linear mode: R2 = S2_linear / ERLE
      const std::array<float, kFftLengthBy2Plus1>& erle = aec_state.Erle();
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        (*R2)[k] = S2_linear[k] / erle[k];
      }
    } else {
      // 非線形モード: R2 ≈ 遠端スペクトル(エコー生成パワー)
      std::array<float, kFftLengthBy2Plus1> X2;
      // 最小実装では線形フィルタ由来の遅延は 0 固定とする。
      ComputeEchoGeneratingPower(render_buffer.GetSpectrumBuffer(), 0, X2);
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        (*R2)[k] = X2[k];
      }
    }
  }

};



// 残留エコーを抑圧する周波数帯ごとのゲインを計算する。
struct SuppressionGain {
  std::array<float, kFftLengthBy2Plus1> last_gain_; // 前回適用したゲイン
  std::array<float, kFftLengthBy2Plus1> last_nearend_{}; // 前回の近端スペクトル
  std::array<float, kFftLengthBy2Plus1> last_echo_{}; // 前回の残留エコースペクトル

  // 近端スペクトルを直近4ブロックで平均するための履歴(過去3ブロック分)
  std::array<std::array<float, kFftLengthBy2Plus1>, 3> nearend_history_{};
  size_t nearend_history_index_ = 0;

  SuppressionGain() {
    last_gain_.fill(1.f);
  }

  // 可聴エコーが残らないようにゲインを制限する。
  // バンド0..5は低域パラメータ、バンド8以上は高域パラメータを使用し、6..7は線形補間。
  // Lf: enr_transparent=0.3f, enr_suppress=0.4f, emr_transparent=0.3f
  // Hf: enr_transparent=0.07f, enr_suppress=0.1f, emr_transparent=0.3f
  void GainToNoAudibleEcho(const std::array<float, kFftLengthBy2Plus1>& nearend,
                           const std::array<float, kFftLengthBy2Plus1>& echo,
                           std::array<float, kFftLengthBy2Plus1>* gain) const {
    for (size_t k = 0; k < gain->size(); ++k) {
      float a;
      if (static_cast<int>(k) <= 5) {
        a = 0.f;
      } else if (static_cast<int>(k) < 8) {
        a = (static_cast<int>(k) - 5) / static_cast<float>(8 - 5);
      } else {
        a = 1.f;
      }
      const float enr_transparent = (1 - a) * 0.3f + a * 0.07f;
      const float enr_suppress = (1 - a) * 0.4f + a * 0.1f;
      const float emr_transparent = (1 - a) * 0.3f + a * 0.3f;
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
    // 近端スペクトルを直近4ブロック(現在+履歴3)で平均する
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      float sum = suppressor_input[k];
      for (const std::array<float, kFftLengthBy2Plus1>& h : nearend_history_) {
        sum += h[k];
      }
      nearend[k] = sum * 0.25f;
    }
    std::copy(suppressor_input.begin(), suppressor_input.end(),
              nearend_history_[nearend_history_index_].begin());
    nearend_history_index_ = (nearend_history_index_ + 1) % nearend_history_.size();
    std::array<float, kFftLengthBy2Plus1> min_gain;
    GetMinGain(residual_echo, last_nearend_, last_echo_, min_gain);
    GainToNoAudibleEcho(nearend, residual_echo, &G);
    for (size_t k = 0; k < gain->size(); ++k) {
      G[k] = std::max(std::min(G[k], max_gain[k]), min_gain[k]);
      (*gain)[k] = std::min((*gain)[k], G[k]);
    }
    std::copy(nearend.begin(), nearend.end(), last_nearend_.begin());
    std::copy(residual_echo.begin(), residual_echo.end(),
              last_echo_.begin());
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
    // 低域でのゲイン減少率上限0.25f
    const float dec = 0.25f;
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
    // ゲインの増加率上限2.0f
    const float inc = 2.0f;
    const float floor = 0.00001f;
    for (size_t k = 0; k < max_gain.size(); ++k) {
      max_gain[k] = std::min(std::max(last_gain_[k] * inc, floor), 1.f);
    }
  }


};

 
// 周波数領域ゲインを適用して残差信号を抑圧するフィルタ。
struct SuppressionFilter {
  std::array<float, kFftLengthBy2> e_output_old_{};

  // 抑圧ゲインを周波数領域残差信号に乗算し、時間領域へ戻して出力する。
  void ApplyGain(const std::array<float, kFftLengthBy2Plus1>& suppression_gain,
                 const FftData& E_lowest_band,
                 Block* e) {
    FftData E = E_lowest_band;
    E.im[0] = E.im[kFftLengthBy2] = 0;
    for (size_t i = 0; i < kFftLengthBy2Plus1; ++i) {
      E.re[i] *= suppression_gain[i];
      E.im[i] *= suppression_gain[i];
    }
    std::array<float, kFftLength> e_extended;
    const float kIfftNormalization = 2.f / static_cast<float>(kFftLength);
    Ifft(E, &e_extended);
    std::span<float, kBlockSize> e0(*e);
    float* e0_old = e_output_old_.data();
    for (size_t i = 0; i < kFftLengthBy2; ++i) {
      float e0_i = e0_old[i] * kSqrtHanning128[kFftLengthBy2 + i];
      e0_i += e_extended[i] * kSqrtHanning128[i];
      e0[i] = e0_i * kIfftNormalization;
    }
    std::copy(e_extended.begin() + kFftLengthBy2, e_extended.begin() + kFftLength, std::begin(e_output_old_));
    for (size_t i = 0; i < kFftLengthBy2; ++i) {
      if (e0[i] < -32768.f) e0[i] = -32768.f;
      if (e0[i] > 32767.f) e0[i] = 32767.f;
    }
  }
};

 

