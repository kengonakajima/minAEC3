#include <stddef.h>
#include <array>
#include <vector>
#include <algorithm>
#include <span>
 

 // FFT 復元から予測誤差を計算するヘルパー。
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
 


// Proves linear echo cancellation functionality
struct Subtractor {
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
  
  

  // Performs the echo subtraction.
  void Process(const RenderBuffer& render_buffer,
               const Block& capture,
               const AecState& aec_state,
               SubtractorOutput* output) {
    // Compute the render powers.
    std::array<float, kFftLengthBy2Plus1> X2;
    render_buffer.SpectralSum(filter_.SizePartitions(), &X2);

    // Process capture (mono)
    {
      SubtractorOutput& out = *output;
      std::span<const float> y = capture.View();
      FftData& E = out.E;
      std::array<float, kBlockSize>& e = out.e;

      FftData S;
      FftData& G = S;

      // 線形フィルタの出力を形成。
      filter_.Filter(render_buffer, &S);
      PredictionError(fft_, S, y, &e, &out.s);

      // Compute the signal powers in the subtractor output.
      out.ComputeMetrics(y);

      // フィルタ出力のFFT。
      fft_.ZeroPaddedFft(e, &E);

      // 将来使用のためスペクトルを計算。
      E.Spectrum(out.E2);

      // フィルタを更新。
      std::array<float, kFftLengthBy2Plus1> erl;
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

 
  const Aec3Fft fft_;
  static constexpr size_t kFilterLengthBlocks = 13;

  AdaptiveFirFilter filter_;
  FilterUpdateGain update_gain_;
  std::vector<std::array<float, kFftLengthBy2Plus1>> frequency_response_;
  
};
