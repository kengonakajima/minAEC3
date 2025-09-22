// FFT 復元から予測誤差を計算するヘルパー。
// fft: FFT演算器, S: フィルタ出力の周波数表現, y: キャプチャ信号,
// e: 残差信号の書き込み先, s: フィルタ出力（推定エコー）の書き込み先
inline void PredictionError(const Aec3Fft& fft,
                            const FftData& S,
                            std::span<const float> y,
                            std::array<float, kBlockSize>* e,
                            std::array<float, kBlockSize>* s) {
  std::array<float, kFftLength> tmp;
  fft.Ifft(S, &tmp);
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
  const Aec3Fft fft_; // エコー推定と残差信号計算に用いるFFTユーティリティ
  static constexpr size_t kFilterLengthBlocks = 13; // 適応FIRフィルタの長さ（ブロック数）
  AdaptiveFirFilter filter_; // 線形エコー推定用の適応FIRフィルタ
  FilterUpdateGain update_gain_; // フィルタ係数を更新するためのゲイン計算器
  std::vector<std::array<float, kFftLengthBy2Plus1>> frequency_response_; // フィルタの周波数応答を保持
    
  Subtractor()
      : fft_(),
        filter_(kFilterLengthBlocks),
        update_gain_(),
        frequency_response_(
            std::vector<std::array<float, kFftLengthBy2Plus1>>(kFilterLengthBlocks)) {
    for (auto& H2_k : frequency_response_) {
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
      std::span<const float> y = capture.View(); // 入力キャプチャ信号
      FftData& E = out.E; // 残差信号の周波数表現
      std::array<float, kBlockSize>& e = out.e; // 残差信号の時間領域配列

      FftData S; // 線形フィルタ出力の周波数表現
      FftData& G = S; // update_gain_.Compute が上書きするゲイン格納先として再利用

      // 線形フィルタの出力を形成。
      filter_.Filter(render_buffer, &S);
      PredictionError(fft_, S, y, &e, &out.s);

      // 減算器出力の信号パワーを計算。
      out.ComputeMetrics(y);

      // 残差信号のFFT。
      fft_.ZeroPaddedFft(e, &E);

      // 将来利用のためスペクトルを保存。
      E.Spectrum(out.E2);

      // フィルタを更新。
      std::array<float, kFftLengthBy2Plus1> erl; // Echo Return Loss（周波数応答）
      ComputeErl(frequency_response_, erl);
      update_gain_.Compute(X2, out, erl,
                           filter_.SizePartitions(),
                           &G);
      filter_.Adapt(render_buffer, G);
      filter_.ComputeFrequencyResponse(&frequency_response_);
    }
  }

  void HandleEchoPathChange(const EchoPathVariability& echo_path_variability) {
    if (echo_path_variability.delay_change != EchoPathVariability::kNone) {
      filter_.HandleEchoPathChange();
      update_gain_.HandleEchoPathChange(echo_path_variability);
    }
  }
};

