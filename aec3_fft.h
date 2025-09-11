#ifndef MODULES_AUDIO_PROCESSING_AEC3_AEC3_FFT_H_
#define MODULES_AUDIO_PROCESSING_AEC3_AEC3_FFT_H_

#include <array>
#include <algorithm>
#include <functional>
#include <span>
#include "ooura_fft.h"
#include "aec3_common.h"
#include "fft_data.h"
#include "window_tables.h"
 

 

// Wrapper class that provides 128 point real valued FFT functionality with the
// FftData type.
struct Aec3Fft {
  Aec3Fft() : ooura_fft_() {}


  // Computes the FFT. Note that both the input and output are modified.
  void Fft(std::array<float, kFftLength>* x, FftData* X) const {
    
    
    ooura_fft_.Fft(x->data());
    X->CopyFromPackedArray(*x);
  }
  // Computes the inverse Fft.
  void Ifft(const FftData& X, std::array<float, kFftLength>* x) const {
    
    X.CopyToPackedArray(x);
    ooura_fft_.InverseFft(x->data());
  }

  // Applies a fixed Hanning window to x, pads kFftLengthBy2 zeros, then FFT。
  void ZeroPaddedFft(std::span<const float> x, FftData* X) const {
    std::array<float, kFftLength> fft;
    std::fill(fft.begin(), fft.begin() + kFftLengthBy2, 0.f);
    // Fixed Hanning window derived from sqrt-Hanning (second half squared).
    for (size_t i = 0; i < kFftLengthBy2; ++i) {
      float w = kSqrtHanning128[kFftLengthBy2 + i];
      w *= w;
      fft[kFftLengthBy2 + i] = x[i] * w;
    }
    Fft(&fft, X);
  }

  // Concatenates the kFftLengthBy2 values long x and x_old before computing the
  // Fft. After that, x is copied to x_old.
  // Padded FFT using fixed sqrt-Hanning window for analysis/synthesis bank。
  void PaddedFft(std::span<const float> x,
                 std::span<const float> x_old,
                 FftData* X) const {
    std::array<float, kFftLength> fft;
    // Fixed sqrt-Hanning window
    std::transform(x_old.begin(), x_old.end(), std::begin(kSqrtHanning128),
                   fft.begin(), std::multiplies<float>());
    std::transform(x.begin(), x.end(),
                   std::begin(kSqrtHanning128) + x_old.size(),
                   fft.begin() + x_old.size(), std::multiplies<float>());
    Fft(&fft, X);
  }

  const OouraFft ooura_fft_;
};

 

#endif  // MODULES_AUDIO_PROCESSING_AEC3_AEC3_FFT_H_
