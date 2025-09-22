// 単一キャプチャチャネルのエコー減算器が返す各種値を保持する。
struct SubtractorOutput {

  std::array<float, kBlockSize> s; // 線形フィルタが再現したエコー（推定スピーカ信号）
  std::array<float, kBlockSize> e; // キャプチャ信号と推定エコーとの差分（残差信号）
  FftData E; // 残差信号eの周波数領域表現
  std::array<float, kFftLengthBy2Plus1> E2; // 残差信号Eのパワースペクトル
  float e2 = 0.f; // 残差信号の時間領域パワー（自乗和）
  float y2 = 0.f; // 入力キャプチャ信号の時間領域パワー（自乗和）

  // 信号のパワー量を更新する。
  void ComputeMetrics(std::span<const float> y) {
    y2 = 0.f;
    for (const float sample : y) {
      y2 += sample * sample;
    }
    e2 = 0.f;
    for (const float residual_sample : e) {
      e2 += residual_sample * residual_sample;
    }
  }
};

 
