// エコーリターンロス強化（ERLE）を推定する。サブバンドごとの推定と全帯域の平均を管理。
struct ErleEstimator {

  const size_t startup_phase_length_blocks_; // スタートアップ期間として更新を抑制するブロック数
  std::array<float, kFftLengthBy2Plus1> erle_; // 周波数ビンごとのERLE推定値
  size_t blocks_since_reset_ = 0; // リセット後に経過したブロック数
    
  ErleEstimator(size_t startup_phase_length_blocks) : startup_phase_length_blocks_(startup_phase_length_blocks) {
    Reset(true);
  }

  // 現在のERLE推定値を返す。
  const std::array<float, kFftLengthBy2Plus1>& Erle() const { return erle_; }

  // 全帯域および各サブバンドのERLE推定値をリセットする。
  void Reset(bool delay_change) {
    erle_.fill(1.f);
    if (delay_change) {
      blocks_since_reset_ = 0;
    }
  }

  // ERLE推定値を最新のスペクトルに基づいて更新する。
  void Update(const std::array<float, kFftLengthBy2Plus1>& capture_spectrum,
              const std::array<float, kFftLengthBy2Plus1>& subtractor_spectrum) {
    const std::array<float, kFftLengthBy2Plus1>& Y2 = capture_spectrum;
    const std::array<float, kFftLengthBy2Plus1>& E2 = subtractor_spectrum;
    if (++blocks_since_reset_ < startup_phase_length_blocks_) {
      return;
    }
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      const float e2 = std::max(E2[k], 1e-6f);
      const float y2 = std::max(Y2[k], 1e-6f);
      const float erle_lin = y2 / e2;
      float max_erle = 64.0f;
      erle_[k] = std::max(1.0f, std::min(erle_lin, max_erle));
    }
  }
};

 
