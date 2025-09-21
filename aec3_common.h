#include <stddef.h>

inline constexpr int kNumBlocksPerSecond = 250;

inline constexpr size_t kFftLengthBy2 = 64;
inline constexpr size_t kFftLengthBy2Plus1 = kFftLengthBy2 + 1;
inline constexpr size_t kFftLengthBy2Minus1 = kFftLengthBy2 - 1;
inline constexpr size_t kFftLength = 2 * kFftLengthBy2;
inline constexpr size_t kFftLengthBy2Log2 = 6;


inline constexpr size_t kBlockSize = kFftLengthBy2;
inline constexpr size_t kBlockSizeLog2 = kFftLengthBy2Log2;

inline constexpr size_t kMatchedFilterWindowSizeSubBlocks = 32;
inline constexpr size_t kMatchedFilterAlignmentShiftSizeSubBlocks =
    kMatchedFilterWindowSizeSubBlocks * 3 / 4;

inline size_t GetDownSampledBufferSize(size_t down_sampling_factor,
                                       size_t num_matched_filters) {
  return kBlockSize / down_sampling_factor *
         (kMatchedFilterAlignmentShiftSizeSubBlocks * num_matched_filters +
          kMatchedFilterWindowSizeSubBlocks + 1);
}

inline size_t GetRenderDelayBufferSize(size_t down_sampling_factor,
                                       size_t num_matched_filters,
                                       size_t filter_length_blocks) {
  return GetDownSampledBufferSize(down_sampling_factor, num_matched_filters) /
             (kBlockSize / down_sampling_factor) +
         filter_length_blocks + 1;
}

