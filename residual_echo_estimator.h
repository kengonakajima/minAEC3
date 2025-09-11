#ifndef MODULES_AUDIO_PROCESSING_AEC3_RESIDUAL_ECHO_ESTIMATOR_H_
#define MODULES_AUDIO_PROCESSING_AEC3_RESIDUAL_ECHO_ESTIMATOR_H_

#include <array>
#include <algorithm>
#include <span>
#include "aec3_common.h"
#include "aec_state.h"
#include "render_buffer.h"
#include "spectrum_buffer.h"
 

 

struct ResidualEchoEstimator {
  // Class-wide constants
  inline static constexpr size_t kNoiseFloorHold = 50;
  inline static constexpr float kMinNoiseFloorPower = 1638400.f;
  inline static constexpr float kStationaryGateSlope = 10.f;
  inline static constexpr float kNoiseGatePower = 27509.42f;
  inline static constexpr float kNoiseGateSlope = 0.3f;
  inline static constexpr size_t kRenderPreWindowSize = 1;
  inline static constexpr size_t kRenderPostWindowSize = 1;
  ResidualEchoEstimator() { Reset(); }

  void Estimate(const AecState& aec_state,
                const RenderBuffer& render_buffer,
                const std::array<float, kFftLengthBy2Plus1>& S2_linear,
                const std::array<float, kFftLengthBy2Plus1>& Y2,
                std::array<float, kFftLengthBy2Plus1>* R2) {
    auto GetRenderIndexesToAnalyze = [](const SpectrumBuffer& spectrum_buffer,
                                        int filter_delay_blocks,
                                        int* idx_start, int* idx_stop) {
      size_t window_start =
          std::max(0, filter_delay_blocks - static_cast<int>(kRenderPreWindowSize));
      size_t window_end =
          filter_delay_blocks + static_cast<int>(kRenderPostWindowSize);
      *idx_start =
          spectrum_buffer.OffsetIndex(spectrum_buffer.read, window_start);
      *idx_stop =
          spectrum_buffer.OffsetIndex(spectrum_buffer.read, window_end + 1);
    };

    auto ApplyNoiseGate = [&](std::span<float, kFftLengthBy2Plus1> X2) {
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        if (kNoiseGatePower > X2[k]) {
          X2[k] = std::max(0.f, X2[k] - kNoiseGateSlope * (kNoiseGatePower - X2[k]));
        }
      }
    };

    auto EchoGeneratingPower = [&](const SpectrumBuffer& spectrum_buffer,
                                   int filter_delay_blocks,
                                   std::span<float, kFftLengthBy2Plus1> X2) {
      int idx_stop;
      int idx_start;
      GetRenderIndexesToAnalyze(spectrum_buffer, filter_delay_blocks,
                                &idx_start, &idx_stop);

      std::fill(X2.begin(), X2.end(), 0.f);
      for (int k = idx_start; k != idx_stop; k = spectrum_buffer.IncIndex(k)) {
        for (size_t j = 0; j < kFftLengthBy2Plus1; ++j) {
          X2[j] = std::max(X2[j], spectrum_buffer.buffer[k][j]);
        }
      }
    };

    // Estimate the power of the stationary noise in the render signal.
    UpdateRenderNoisePower(render_buffer);

    if (aec_state.UsableLinearEstimate()) {
      // Linear mode: R2 = S2_linear / ERLE
      const auto& erle = aec_state.Erle();
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        (*R2)[k] = S2_linear[k] / erle[k];
      }
    } else {
      // Nonlinear mode: use echo generating power with gating and noise floor subtraction
      const float echo_path_gain = 1.0f;  // simplified
      std::array<float, kFftLengthBy2Plus1> X2;
      EchoGeneratingPower(render_buffer.GetSpectrumBuffer(),
                          aec_state.MinDirectPathFilterDelay(), X2);
      ApplyNoiseGate(X2);
      for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
        X2[k] -= kStationaryGateSlope * X2_noise_floor_[k];
        X2[k] = std::max(0.f, X2[k]);
        (*R2)[k] = X2[k] * echo_path_gain;
      }
    }
  }

  // Resets the state.
  void Reset() {
    X2_noise_floor_counter_.fill(static_cast<int>(kNoiseFloorHold));
    X2_noise_floor_.fill(kMinNoiseFloorPower);
  }

  // Updates estimate for the power of the stationary noise component in the
  // render signal.
  void UpdateRenderNoisePower(const RenderBuffer& render_buffer) {
    const auto& render_power = render_buffer.Spectrum(0);
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      if (render_power[k] < X2_noise_floor_[k]) {
        X2_noise_floor_[k] = render_power[k];
        X2_noise_floor_counter_[k] = 0;
      } else {
        if (X2_noise_floor_counter_[k] >= static_cast<int>(kNoiseFloorHold)) {
          X2_noise_floor_[k] =
              std::max(X2_noise_floor_[k] * 1.1f, kMinNoiseFloorPower);
        } else {
          ++X2_noise_floor_counter_[k];
        }
      }
    }
  }

  std::array<float, kFftLengthBy2Plus1> X2_noise_floor_;
  std::array<int, kFftLengthBy2Plus1> X2_noise_floor_counter_;

};

 

#endif  // MODULES_AUDIO_PROCESSING_AEC3_RESIDUAL_ECHO_ESTIMATOR_H_
