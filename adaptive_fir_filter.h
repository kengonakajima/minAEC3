// フィルタの周波数応答を計算して保持する。
// num_partitions: パーティション数, H: フィルタ係数のFFT結果, H2: 出力先
inline void ComputeFrequencyResponse(
    size_t num_partitions,
    const std::vector<FftData>& H,
    std::vector<std::array<float, kFftLengthBy2Plus1>>* H2) {
  for (auto& H2_ch : *H2) {
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
  for (auto& H2_j : H2) {
    std::transform(H2_j.begin(), H2_j.end(), erl.begin(), erl.begin(),
                   std::plus<float>());
  }
}

 

// 周波数領域で動作する適応フィルタを提供する。
struct AdaptiveFirFilter {
  const Aec3Fft fft_; // フィルタ更新に用いるFFTヘルパー
  const size_t size_partitions_; // ブロック単位のパーティション数
  std::vector<FftData> H_; // 各パーティションの周波数領域係数
  size_t partition_to_constrain_ = 0; // 正規化対象のパーティションインデックス
    
  AdaptiveFirFilter(size_t size_partitions)
      : fft_(), size_partitions_(size_partitions), H_(size_partitions) {
    for (size_t p = 0; p < H_.size(); ++p) {
      H_[p].Clear();
    }
  }

  


  // フィルタ出力を生成する。
  // render_buffer: レンダーFFTバッファ, S: 出力先
  void Filter(const RenderBuffer& render_buffer, FftData* S) const {
    ::ApplyFilter(render_buffer, size_partitions_, H_, S);
  }

  // フィルタ係数を適応更新する。
  // render_buffer: レンダーFFTバッファ, G: 更新ゲイン
  void Adapt(const RenderBuffer& render_buffer, const FftData& G) {
    ::AdaptPartitions(render_buffer, G, size_partitions_, &H_);
    Constrain();
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
      fft_.Ifft(H_[partition_to_constrain_], &h);
      static const float kScale = 1.0f / static_cast<float>(kFftLengthBy2);
      std::for_each(h.begin(), h.begin() + kFftLengthBy2,
                    [](float& a) { a *= kScale; });
      std::fill(h.begin() + kFftLengthBy2, h.end(), 0.f);
      fft_.Fft(&h, &H_[partition_to_constrain_]);
    }
    partition_to_constrain_ =
        partition_to_constrain_ < (size_partitions_ - 1)
            ? partition_to_constrain_ + 1
            : 0;
  }


};
