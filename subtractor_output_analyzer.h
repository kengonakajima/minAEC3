#ifndef MODULES_AUDIO_PROCESSING_AEC3_SUBTRACTOR_OUTPUT_ANALYZER_H_
#define MODULES_AUDIO_PROCESSING_AEC3_SUBTRACTOR_OUTPUT_ANALYZER_H_

#include "subtractor_output.h"
#include "aec3_common.h"

 

// Class for analyzing the properties subtractor output.
struct SubtractorOutputAnalyzer {
  SubtractorOutputAnalyzer() {}
  ~SubtractorOutputAnalyzer() = default;

  // Analyses the subtractor output.
  void Update(const SubtractorOutput& subtractor_output,
              bool* any_filter_converged) {
    *any_filter_converged = false;
    const float y2 = subtractor_output.y2;
    const float e2 = subtractor_output.e2;
    const float kConvergenceThreshold = 50 * 50 * static_cast<float>(kBlockSize);
    bool filter_converged_now = e2 < 0.5f * y2 && y2 > kConvergenceThreshold;
    filter_converged_ = filter_converged_now;
    *any_filter_converged = filter_converged_;
  }

  // Handle echo path change.
  void HandleEchoPathChange() { filter_converged_ = false; }

  bool filter_converged_ = false;
};

 

#endif  // MODULES_AUDIO_PROCESSING_AEC3_SUBTRACTOR_OUTPUT_ANALYZER_H_
