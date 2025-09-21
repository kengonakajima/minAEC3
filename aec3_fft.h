// 128ポイント実数FFTをFftData型と組み合わせて提供するラッパークラス。
struct Aec3Fft {
  const OouraFft ooura_fft_; // 実際のFFT演算を担当するOoura FFT実装
    
  Aec3Fft() : ooura_fft_() {}


  // FFTを計算する（入力配列と出力FftDataの両方が更新される）。
  void Fft(std::array<float, kFftLength>* x, FftData* X) const {
    ooura_fft_.Fft(x->data());
    X->CopyFromPackedArray(*x);
  }
  // 逆FFTを計算する。
  void Ifft(const FftData& X, std::array<float, kFftLength>* x) const {
    X.CopyToPackedArray(x);
    ooura_fft_.InverseFft(x->data());
  }

  // 固定ハニング窓を適用し、後半にゼロを詰めてからFFTを計算する。
  void ZeroPaddedFft(std::span<const float> x, FftData* X) const {
    std::array<float, kFftLength> fft;
    std::fill(fft.begin(), fft.begin() + kFftLengthBy2, 0.f);
    // sqrt-Hanningの後半を二乗した固定ハニング窓を利用。
    for (size_t i = 0; i < kFftLengthBy2; ++i) {
      float w = kSqrtHanning128[kFftLengthBy2 + i];
      w *= w;
      fft[kFftLengthBy2 + i] = x[i] * w;
    }
    Fft(&fft, X);
  }

  // 過去ブロックx_oldと現在ブロックxを連結し、固定sqrt-Hanning窓でパディングFFTを行う。
  // 呼び出し側では処理後にxをx_oldへコピーして解析・合成バンクを継続する想定。
  void PaddedFft(std::span<const float> x,
                 std::span<const float> x_old,
                 FftData* X) const {
    std::array<float, kFftLength> fft;
    // 固定sqrt-Hanning窓を適用
    std::transform(x_old.begin(), x_old.end(), std::begin(kSqrtHanning128),
                   fft.begin(), std::multiplies<float>());
    std::transform(x.begin(), x.end(),
                   std::begin(kSqrtHanning128) + x_old.size(),
                   fft.begin() + x_old.size(), std::multiplies<float>());
    Fft(&fft, X);
  }
};

 
