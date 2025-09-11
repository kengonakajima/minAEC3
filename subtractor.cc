#include "subtractor.h"

#include <algorithm>

#include "adaptive_fir_filter.h"
 
 

 

static void PredictionError(const Aec3Fft& fft,
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

Subtractor::Subtractor()
    : fft_(),
      filter_(kFilterLengthBlocks),
      update_gain_(),
      frequency_response_(
          std::vector<std::array<float, kFftLengthBy2Plus1>>(kFilterLengthBlocks)) {
  

  
  for (auto& H2_k : frequency_response_) {
    H2_k.fill(0.f);
  }
}


void Subtractor::HandleEchoPathChange(
    const EchoPathVariability& echo_path_variability) {
  const auto full_reset = [&]() {
    filter_.HandleEchoPathChange();
    update_gain_.HandleEchoPathChange(echo_path_variability);
  };

  if (echo_path_variability.delay_change !=
      EchoPathVariability::kNone) {
    full_reset();
  }

}

// 初期/本稼働の二段階は廃止（固定設定）。

void Subtractor::Process(const RenderBuffer& render_buffer,
                         const Block& capture,
                         const AecState& aec_state,
                         SubtractorOutput* outputs) {
  

  // Compute the render powers.
  std::array<float, kFftLengthBy2Plus1> X2;
  render_buffer.SpectralSum(filter_.SizePartitions(), &X2);

  // Process capture (mono)
  {
    SubtractorOutput& output = *outputs;
    std::span<const float> y = capture.View();
    FftData& E = output.E;
    std::array<float, kBlockSize>& e = output.e;

    FftData S;
    FftData& G = S;

    // 線形フィルタ出力。
    filter_.Filter(render_buffer, &S);
    PredictionError(fft_, S, y, &e, &output.s);

    // Compute the signal powers in the subtractor output.
    output.ComputeMetrics(y);

    // フィルタ出力のFFT。
    fft_.ZeroPaddedFft(e, &E);

    // スペクトル計算。
    E.Spectrum(output.E2);

    // フィルタ更新。
    std::array<float, kFftLengthBy2Plus1> erl;
    ComputeErl(frequency_response_, erl);
    update_gain_.Compute(X2, output, erl,
                         filter_.SizePartitions(),
                         &G);
    filter_.Adapt(render_buffer, G);
    filter_.ComputeFrequencyResponse(&frequency_response_);

    

    // ここでのクランプは出力段で再実施されるため省略。
  }
}

 

 
