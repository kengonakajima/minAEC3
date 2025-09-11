#ifndef MODULES_AUDIO_PROCESSING_AEC3_ERLE_ESTIMATOR_H_
#define MODULES_AUDIO_PROCESSING_AEC3_ERLE_ESTIMATOR_H_

#include <stddef.h>

#include <array>
#include <algorithm>
#include "aec3_common.h"

 

// Estimates the echo return loss enhancement. One estimate is done per subband
// and another one is done using the aggreation of energy over all the subbands.
struct ErleEstimator {
  ErleEstimator(size_t startup_phase_length_blocks)
      : startup_phase_length_blocks_(startup_phase_length_blocks) {
    Reset(true);
  }
  

  // Resets the fullband ERLE estimator and the subbands ERLE estimators.
  void Reset(bool delay_change) {
    erle_.fill(1.f);
    if (delay_change) {
      blocks_since_reset_ = 0;
    }
  }

  // Updates the ERLE estimates.
  void Update(const std::array<float, kFftLengthBy2Plus1>& capture_spectrum,
              const std::array<float, kFftLengthBy2Plus1>& subtractor_spectrum) {
    const auto& Y2 = capture_spectrum;
    const auto& E2 = subtractor_spectrum;
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

  const std::array<float, kFftLengthBy2Plus1>& Erle() const { return erle_; }

  // Returns the non-capped subband ERLE.
  // Unbounded ERLE の公開は削除。

  

  const size_t startup_phase_length_blocks_;
  std::array<float, kFftLengthBy2Plus1> erle_;
  size_t blocks_since_reset_ = 0;
};

 

#endif  // MODULES_AUDIO_PROCESSING_AEC3_ERLE_ESTIMATOR_H_
