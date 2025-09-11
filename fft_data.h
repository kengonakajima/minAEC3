#ifndef MODULES_AUDIO_PROCESSING_AEC3_FFT_DATA_H_
#define MODULES_AUDIO_PROCESSING_AEC3_FFT_DATA_H_

 
#include <algorithm>
#include <array>

#include <span>
#include "aec3_common.h"

 

// Struct that holds imaginary data produced from 128 point real-valued FFTs.
struct FftData {
  // Copies the data in src.
  void Assign(const FftData& src) {
    std::copy(src.re.begin(), src.re.end(), re.begin());
    std::copy(src.im.begin(), src.im.end(), im.begin());
    im[0] = im[kFftLengthBy2] = 0;
  }

  // Clears all the imaginary.
  void Clear() {
    re.fill(0.f);
    im.fill(0.f);
  }

  // Computes the power spectrum of the data.
  void Spectrum(std::span<float> power_spectrum) const {
    
    std::transform(re.begin(), re.end(), im.begin(), power_spectrum.begin(),
                   [](float a, float b) { return a * a + b * b; });
  }

  // Copy the data from array input.
  void CopyFromPackedArray(const std::array<float, kFftLength>& v) {
    re[0] = v[0];
    re[kFftLengthBy2] = v[1];
    im[0] = im[kFftLengthBy2] = 0;
    for (size_t k = 1, j = 2; k < kFftLengthBy2; ++k) {
      re[k] = v[j++];
      im[k] = v[j++];
    }
  }

  // Copies the data into an array.
  void CopyToPackedArray(std::array<float, kFftLength>* v) const {
    
    (*v)[0] = re[0];
    (*v)[1] = re[kFftLengthBy2];
    for (size_t k = 1, j = 2; k < kFftLengthBy2; ++k) {
      (*v)[j++] = re[k];
      (*v)[j++] = im[k];
    }
  }

  std::array<float, kFftLengthBy2Plus1> re;
  std::array<float, kFftLengthBy2Plus1> im;
};

 

#endif  // MODULES_AUDIO_PROCESSING_AEC3_FFT_DATA_H_
