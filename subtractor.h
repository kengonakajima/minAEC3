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
// フィルタの周波数応答を計算して保持する。
// num_partitions: パーティション数, H: フィルタ係数のFFT結果, H2: 出力先
inline void ComputeFrequencyResponse(
    size_t num_partitions,
    const std::vector<FftData>& H,
    std::vector<std::array<float, kFftLengthBy2Plus1>>* H2) {
  for (std::array<float, kFftLengthBy2Plus1>& H2_ch : *H2) {
    H2_ch.fill(0.f);
  }
  for (size_t p = 0; p < num_partitions; ++p) {
    for (size_t j = 0; j < kFftLengthBy2Plus1; ++j) {
      float tmp = H[p].re[j] * H[p].re[j] + H[p].im[j] * H[p].im[j];
      (*H2)[p][j] = std::max((*H2)[p][j], tmp);
    }
  }
}

// フィルタ係数の各パーティションを適応更新する。
// render_buffer: レンダーFFTバッファ, G: 更新ゲイン, num_partitions: パーティション数, H: フィルタ係数格納先
inline void AdaptPartitions(const RenderBuffer& render_buffer,
                            const FftData& G,
                            size_t num_partitions,
                            std::vector<FftData>* H) {
  std::span<const FftData> render_buffer_data = render_buffer.GetFftBuffer();
  size_t index = render_buffer.Position();
  for (size_t p = 0; p < num_partitions; ++p) {
    const FftData& X_p = render_buffer_data[index];
    FftData& H_p = (*H)[p];
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      H_p.re[k] += X_p.re[k] * G.re[k] + X_p.im[k] * G.im[k];
      H_p.im[k] += X_p.re[k] * G.im[k] - X_p.im[k] * G.re[k];
    }
    index = index < (render_buffer_data.size() - 1) ? index + 1 : 0;
  }
}

// フィルタ出力（周波数領域）を生成する。
// render_buffer: レンダーFFTバッファ, num_partitions: パーティション数, H: フィルタ係数, S: 出力先
inline void ApplyFilter(const RenderBuffer& render_buffer,
                        size_t num_partitions,
                        const std::vector<FftData>& H,
                        FftData* S) {
  S->re.fill(0.f);
  S->im.fill(0.f);
  std::span<const FftData> render_buffer_data = render_buffer.GetFftBuffer();
  size_t index = render_buffer.Position();
  for (size_t p = 0; p < num_partitions; ++p) {
    const FftData& X_p = render_buffer_data[index];
    const FftData& H_p = H[p];
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      S->re[k] += X_p.re[k] * H_p.re[k] - X_p.im[k] * H_p.im[k];
      S->im[k] += X_p.re[k] * H_p.im[k] + X_p.im[k] * H_p.re[k];
    }
    index = index < (render_buffer_data.size() - 1) ? index + 1 : 0;
  }
}

// 周波数応答を合計して Echo Return Loss を算出する。
// H2: パーティション別のパワースペクトル, erl: 出力先
inline void ComputeErl(
    const std::vector<std::array<float, kFftLengthBy2Plus1>>& H2,
    std::span<float> erl) {
  std::fill(erl.begin(), erl.end(), 0.f);
  for (const std::array<float, kFftLengthBy2Plus1>& H2_j : H2) {
    std::transform(H2_j.begin(), H2_j.end(), erl.begin(), erl.begin(),
                   std::plus<float>());
  }
}

 

// 周波数領域で動作する適応フィルタを提供する。
struct AdaptiveFirFilter {
  const size_t size_partitions_; // ブロック単位のパーティション数
  std::vector<FftData> H_; // 各パーティションの周波数領域係数
  size_t partition_to_constrain_ = 0; // 正規化対象のパーティションインデックス

  AdaptiveFirFilter(size_t size_partitions)
      : size_partitions_(size_partitions), H_(size_partitions) {
    for (size_t p = 0; p < H_.size(); ++p) {
      H_[p].Clear();
    }
  }

  


  // 既知のエコーパス変化が発生したときにフィルタ係数を初期化する。
  void HandleEchoPathChange() {
    for (size_t p = 0; p < H_.size(); ++p) {
      H_[p].Clear();
    }
  }

  // フィルタのパーティション数（固定）を返す。
  size_t SizePartitions() const { return size_partitions_; }

  // 各パーティションの周波数応答を計算する。
  // H2: 出力先
  void ComputeFrequencyResponse(
      std::vector<std::array<float, kFftLengthBy2Plus1>>* H2) const {
    H2->resize(size_partitions_);
    ::ComputeFrequencyResponse(size_partitions_, H_, H2);
  }

  // フィルタパーティションを巡回しながら正規化する。
  void Constrain() {
    std::array<float, kFftLength> h;
    {
      Ifft(H_[partition_to_constrain_], &h);
      static const float kScale = 1.0f / static_cast<float>(kFftLengthBy2);
      std::for_each(h.begin(), h.begin() + kFftLengthBy2,
                    [](float& a) { a *= kScale; });
      std::fill(h.begin() + kFftLengthBy2, h.end(), 0.f);
      Fft(&h, &H_[partition_to_constrain_]);
    }
    partition_to_constrain_ =
        partition_to_constrain_ < (size_partitions_ - 1)
            ? partition_to_constrain_ + 1
            : 0;
  }


};
// 自己回帰型の線形フィルタ更新ゲインを計算する。
struct FilterUpdateGain {

  static constexpr float kLeakageConverged = 0.00005f; // 収束時に誤差推定へ加えるリーク係数
  static constexpr float kLeakageDiverged = 0.05f; // 発散時に誤差推定へ加えるリーク係数
  static constexpr float kErrorFloor = 0.001f; // 誤差推定H_error_の下限値
  static constexpr float kErrorCeil = 2.f; // 誤差推定H_error_の上限値
  static constexpr float kNoiseGate = 20075344.f; // レンダーパワーがこの値未満なら更新を抑制
  static constexpr float kHErrorInitial = 10000.f; // 誤差推定の初期値
  std::array<float, kFftLengthBy2Plus1> H_error_; // 各周波数ビンのフィルタ誤差推定値
  size_t call_counter_ = 0; // Computeを呼び出した累計回数(パーティション履歴が満タンになるまで更新を止める)

  FilterUpdateGain() {
    H_error_.fill(kHErrorInitial);
  }



  // 既知のエコーパス変化が発生した際のリセット処理。
  void HandleEchoPathChange(EchoPathVariability echo_path_variability) {
    if (echo_path_variability != EchoPathVariability::kNone) {
      H_error_.fill(kHErrorInitial);
    }
    call_counter_ = 0;
  }

  // 更新ゲインを計算する。
  // render_power: レンダー信号パワー, subtractor_output: 減算器の出力統計, erl: 推定ERL, size_partitions: パーティション数, gain_fft: 出力先
  void Compute(const std::array<float, kFftLengthBy2Plus1>& render_power,
               const SubtractorOutput& subtractor_output,
               std::span<const float> erl,
               size_t size_partitions,
               FftData* gain_fft) {
    const FftData& E = subtractor_output.E;
    const std::array<float, kFftLengthBy2Plus1>& E2 = subtractor_output.E2;
    FftData* G = gain_fft;
    const std::array<float, kFftLengthBy2Plus1>& X2 = render_power;
    ++call_counter_;
    if (call_counter_ <= size_partitions) {
      // パーティション履歴が満タンになるまで更新を止める(PFDAFの初期暴発を防ぐ)
      G->re.fill(0.f);
      G->im.fill(0.f);
    } else {
      std::array<float, kFftLengthBy2Plus1> mu;
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        if (X2[k] >= kNoiseGate) {
          mu[k] = H_error_[k] /
                  (0.5f * H_error_[k] * X2[k] + size_partitions * E2[k]);
        } else {
          mu[k] = 0.f;
        }
      }
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        H_error_[k] -= 0.5f * mu[k] * X2[k] * H_error_[k];
      }
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        G->re[k] = mu[k] * E.re[k];
        G->im[k] = mu[k] * E.im[k];
      }
    }
    const bool filter_ok = (subtractor_output.e2 <= 0.5f * subtractor_output.y2);
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      if (filter_ok) {
        H_error_[k] += kLeakageConverged * erl[k];
      } else {
        H_error_[k] += kLeakageDiverged * erl[k];
      }
      H_error_[k] = std::max(H_error_[k], kErrorFloor);
      H_error_[k] = std::min(H_error_[k], kErrorCeil);
    }
  }  
};

// FFT 復元から予測誤差を計算するヘルパー。
// S: フィルタ出力の周波数表現, y: キャプチャ信号,
// e: 残差信号の書き込み先, s: フィルタ出力（推定エコー）の書き込み先
inline void PredictionError(const FftData& S,
                            std::span<const float> y,
                            std::array<float, kBlockSize>* e,
                            std::array<float, kBlockSize>* s) {
  std::array<float, kFftLength> tmp;
  Ifft(S, &tmp);
  const float kScale = 1.0f / static_cast<float>(kFftLengthBy2);
  std::transform(y.begin(), y.end(), tmp.begin() + kFftLengthBy2, e->begin(),
                 [&](float a, float b) { return a - b * kScale; });
  if (s) {
    for (size_t k = 0; k < s->size(); ++k) {
      (*s)[k] = kScale * tmp[k + kFftLengthBy2];
    }
  }
}
 


// 線形エコーキャンセルを実装する中核コンポーネント。
struct Subtractor {
  static constexpr size_t kFilterLengthBlocks = 13; // 適応FIRフィルタの長さ（ブロック数）
  AdaptiveFirFilter filter_; // 線形エコー推定用の適応FIRフィルタ
  FilterUpdateGain update_gain_; // フィルタ係数を更新するためのゲイン計算器
  std::vector<std::array<float, kFftLengthBy2Plus1>> frequency_response_; // フィルタの周波数応答を保持

  Subtractor()
      : filter_(kFilterLengthBlocks),
        update_gain_(),
        frequency_response_(
            std::vector<std::array<float, kFftLengthBy2Plus1>>(kFilterLengthBlocks)) {
    for (std::array<float, kFftLengthBy2Plus1>& H2_k : frequency_response_) {
      H2_k.fill(0.f);
    }
  }
  

  // エコー減算処理を実行する。
  void Process(const RenderBuffer& render_buffer,
               const Block& capture,
               const AecState& aec_state,
               SubtractorOutput* output) {
    // レンダー信号のスペクトルパワーを算出。
    std::array<float, kFftLengthBy2Plus1> X2;
    render_buffer.SpectralSum(filter_.SizePartitions(), &X2);

    // キャプチャ信号（モノラル）の処理本体。
    {
      SubtractorOutput& out = *output; // 出力構造体への参照
      std::span<const float> y = capture; // 入力キャプチャ信号
      FftData& E = out.E; // 残差信号の周波数表現
      std::array<float, kBlockSize>& e = out.e; // 残差信号の時間領域配列

      FftData S; // 線形フィルタ出力の周波数表現
      FftData& G = S; // update_gain_.Compute が上書きするゲイン格納先として再利用

      // 線形フィルタの出力を形成。
      ApplyFilter(render_buffer, filter_.size_partitions_, filter_.H_, &S);
      PredictionError(S, y, &e, &out.s);

      // 減算器出力の信号パワーを計算。
      out.ComputeMetrics(y);

      // 残差信号のFFT。
      ZeroPaddedFft(e, &E);

      // 将来利用のためスペクトルを保存。
      E.Spectrum(out.E2);

      // フィルタを更新。
      std::array<float, kFftLengthBy2Plus1> erl; // Echo Return Loss（周波数応答）
      ComputeErl(frequency_response_, erl);
      update_gain_.Compute(X2, out, erl,
                           filter_.size_partitions_,
                           &G);
      AdaptPartitions(render_buffer, G, filter_.size_partitions_, &filter_.H_);
      filter_.Constrain();
      frequency_response_.resize(filter_.size_partitions_);
      ComputeFrequencyResponse(filter_.size_partitions_, filter_.H_, &frequency_response_);
    }
  }

  void HandleEchoPathChange(EchoPathVariability echo_path_variability) {
    if (echo_path_variability != EchoPathVariability::kNone) {
      filter_.HandleEchoPathChange();
      update_gain_.HandleEchoPathChange(echo_path_variability);
    }
  }
};

