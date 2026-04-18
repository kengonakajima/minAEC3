
// Echo remover の動作状態を管理。
struct AecState {
  AecState() : erle_estimator_(2 * kNumBlocksPerSecond) {}

  // エコー減算器の線形推定が残留エコー推定に利用できるかを返す。
  // 起動直後は線形フィルタが未収束なので非線形モードで動かし、
  // kWarmupBlocks ブロック経過後に線形モードへ切り替える。
  bool UsableLinearEstimate() const { return blocks_since_reset_ > kWarmupBlocks; }

  const std::array<float, kFftLengthBy2Plus1>& Erle() const {
    return erle_estimator_.Erle();
  }

  // 線形フィルタに基づく遅延推定値を返す（簡略化により常に0）。
  int MinDirectPathFilterDelay() const { return 0; }

  // エコーパス変化時に必要なリセット処理を行う。
  void HandleEchoPathChange(EchoPathVariability echo_path_variability) {
    if (echo_path_variability != EchoPathVariability::kNone) {
      erle_estimator_.Reset(true);
      blocks_since_reset_ = 0;
    }
  }

  // 最新の信号情報でAEC状態を更新する。
  void Update(const std::array<float, kFftLengthBy2Plus1>& E2,
              const std::array<float, kFftLengthBy2Plus1>& Y2) {
    erle_estimator_.Update(Y2, E2);
    ++blocks_since_reset_;
  }

  // 起動/遅延リセット後、線形モードを有効化するまでの待機ブロック数 (約0.4秒)。
  static constexpr size_t kWarmupBlocks = kNumBlocksPerSecond * 2 / 5;
  size_t blocks_since_reset_ = 0;
  ErleEstimator erle_estimator_;
};
