
struct SuppressionFilter {
  SuppressionFilter() : fft_() { e_output_old_.fill(0.f); }
  


  void ApplyGain(const std::array<float, kFftLengthBy2Plus1>& suppression_gain,
                 const FftData& E_lowest_band,
                 Block* e) {
    FftData E;
    E.Assign(E_lowest_band);
    for (size_t i = 0; i < kFftLengthBy2Plus1; ++i) {
      E.re[i] *= suppression_gain[i];
      E.im[i] *= suppression_gain[i];
    }
    std::array<float, kFftLength> e_extended;
    const float kIfftNormalization = 2.f / static_cast<float>(kFftLength);
    fft_.Ifft(E, &e_extended);
    auto e0 = e->View();
    float* e0_old = e_output_old_.data();
    for (size_t i = 0; i < kFftLengthBy2; ++i) {
      float e0_i = e0_old[i] * kSqrtHanning128[kFftLengthBy2 + i];
      e0_i += e_extended[i] * kSqrtHanning128[i];
      e0[i] = e0_i * kIfftNormalization;
    }
    std::copy(e_extended.begin() + kFftLengthBy2,
              e_extended.begin() + kFftLength, std::begin(e_output_old_));
    for (size_t i = 0; i < kFftLengthBy2; ++i) {
      if (e0[i] < -32768.f) e0[i] = -32768.f;
      if (e0[i] > 32767.f) e0[i] = 32767.f;
    }
  }

  const Aec3Fft fft_;
  std::array<float, kFftLengthBy2> e_output_old_{};
};

 


