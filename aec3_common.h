// AEC3で共通利用する定数群。元のWebRTC実装から必要最小限を抽出。
#pragma once
inline constexpr int kNumBlocksPerSecond = 250; // 16 kHz, 64サンプルブロックで1秒あたりのブロック数
inline constexpr size_t kFftLengthBy2 = 64; // FFTで扱う片側サイズ(周波数ビン数)
inline constexpr size_t kFftLengthBy2Plus1 = kFftLengthBy2 + 1; // DC〜Nyquistまでのビン数(実数FFT用) (65)
inline constexpr size_t kFftLengthBy2Minus1 = kFftLengthBy2 - 1; // Nyquistを除いた高周波数側のビン数 (63)
inline constexpr size_t kFftLength = 2 * kFftLengthBy2; // 実際にIFFTするサンプル長(128)
inline constexpr size_t kFftLengthBy2Log2 = 6; // kFftLengthBy2 (=64) のlog2値


inline constexpr size_t kBlockSize = kFftLengthBy2; // 1処理ブロックのサンプル数(64)
inline constexpr size_t kBlockSizeLog2 = kFftLengthBy2Log2; // ブロックサイズのlog2値(6)

inline constexpr size_t kMatchedFilterWindowSizeSubBlocks = 32; // 遅延推定で用いる窓幅(サブブロック数)
inline constexpr size_t kMatchedFilterAlignmentShiftSizeSubBlocks =
    kMatchedFilterWindowSizeSubBlocks * 3 / 4;  // 遅延探索時のシフト間隔(サブブロック数) 

// ダウンサンプリング後のレンダーバッファサイズを求める。
// MatchedFilter はサブブロック単位（kBlockSize / down_sampling_factor サンプル）で
// 遅延候補を探索する。各フィルタの窓幅は kMatchedFilterWindowSizeSubBlocks サブブロック、
// 連続するフィルタ間は kMatchedFilterAlignmentShiftSizeSubBlocks サブブロックだけシフトして配置する。
// したがって、必要なサブブロック総数は
//   alignment_shift * num_matched_filters + window + 1
// となる（+1 は循環バッファの境界オーバーランを防ぐ余裕）。
// これにサブブロック長 (kBlockSize / down_sampling_factor) を掛けるとバッファ長（サンプル数）が得られる。
inline size_t GetDownSampledBufferSize(size_t down_sampling_factor, size_t num_matched_filters) {
  return kBlockSize / down_sampling_factor *
         (kMatchedFilterAlignmentShiftSizeSubBlocks * num_matched_filters +
          kMatchedFilterWindowSizeSubBlocks + 1);
}

// レンダー遅延バッファの必要長を算出する。
// GetDownSampledBufferSize で得たサンプル長をサブブロック長で割ると、ダウンサンプル領域で
// 必要なサブブロック数になる。それに線形フィルタのパーティション長
// (filter_length_blocks) と余裕（+1）を加えることで、フルバンドのブロック数としての
// バッファ長を確保する。
inline size_t GetRenderDelayBufferSize(size_t down_sampling_factor,
                                       size_t num_matched_filters,
                                       size_t filter_length_blocks) {
  return GetDownSampledBufferSize(down_sampling_factor, num_matched_filters) /
             (kBlockSize / down_sampling_factor) +
         filter_length_blocks + 1;
}
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include "aec3_common.h"

// 4ミリ秒分のモノラル音声データ（64サンプル=4ms、16kHzモノラル）。
using Block = std::array<float, kBlockSize>;

// 16bit PCMからブロックへコピーする。
inline void CopyFromPcm16(const int16_t* src, Block* dst) {
  for (size_t i = 0; i < dst->size(); ++i) {
    (*dst)[i] = static_cast<float>(src[i]);
  }
}

// ブロック内容を16bit PCMへ書き出す。
inline void CopyToPcm16(const Block& src, int16_t* dst) {
  for (size_t i = 0; i < src.size(); ++i) {
    float v = src[i];
    v = std::min(v, 32767.f);
    v = std::max(v, -32768.f);
    dst[i] = static_cast<int16_t>(v + std::copysign(0.5f, v));
  }
}
inline int IncIndex(int index, int size) {
  return index < size - 1 ? index + 1 : 0;
}

inline int DecIndex(int index, int size) {
  return index > 0 ? index - 1 : size - 1;
}

inline int OffsetIndex(int index, int offset, int size) {
  return (size + index + offset) % size;
}


// OLA分析/合成で用いる128ポイントの平方根ハニング窓係数。
inline constexpr std::array<float, kFftLength> kSqrtHanning128 = {
    0.00000000000000f, 0.02454122852291f, 0.04906767432742f, 0.07356456359967f,
    0.09801714032956f, 0.12241067519922f, 0.14673047445536f, 0.17096188876030f,
    0.19509032201613f, 0.21910124015687f, 0.24298017990326f, 0.26671275747490f,
    0.29028467725446f, 0.31368174039889f, 0.33688985339222f, 0.35989503653499f,
    0.38268343236509f, 0.40524131400499f, 0.42755509343028f, 0.44961132965461f,
    0.47139673682600f, 0.49289819222978f, 0.51410274419322f, 0.53499761988710f,
    0.55557023301960f, 0.57580819141785f, 0.59569930449243f, 0.61523159058063f,
    0.63439328416365f, 0.65317284295378f, 0.67155895484702f, 0.68954054473707f,
    0.70710678118655f, 0.72424708295147f, 0.74095112535496f, 0.75720884650648f,
    0.77301045336274f, 0.78834642762661f, 0.80320753148064f, 0.81758481315158f,
    0.83146961230255f, 0.84485356524971f, 0.85772861000027f, 0.87008699110871f,
    0.88192126434835f, 0.89322430119552f, 0.90398929312344f, 0.91420975570353f,
    0.92387953251129f, 0.93299279883474f, 0.94154406518302f, 0.94952818059304f,
    0.95694033573221f, 0.96377606579544f, 0.97003125319454f, 0.97570213003853f,
    0.98078528040323f, 0.98527764238894f, 0.98917650996478f, 0.99247953459871f,
    0.99518472667220f, 0.99729045667869f, 0.99879545620517f, 0.99969881869620f,
    1.00000000000000f, 0.99969881869620f, 0.99879545620517f, 0.99729045667869f,
    0.99518472667220f, 0.99247953459871f, 0.98917650996478f, 0.98527764238894f,
    0.98078528040323f, 0.97570213003853f, 0.97003125319454f, 0.96377606579544f,
    0.95694033573221f, 0.94952818059304f, 0.94154406518302f, 0.93299279883474f,
    0.92387953251129f, 0.91420975570353f, 0.90398929312344f, 0.89322430119552f,
    0.88192126434835f, 0.87008699110871f, 0.85772861000027f, 0.84485356524971f,
    0.83146961230255f, 0.81758481315158f, 0.80320753148064f, 0.78834642762661f,
    0.77301045336274f, 0.75720884650648f, 0.74095112535496f, 0.72424708295147f,
    0.70710678118655f, 0.68954054473707f, 0.67155895484702f, 0.65317284295378f,
    0.63439328416365f, 0.61523159058063f, 0.59569930449243f, 0.57580819141785f,
    0.55557023301960f, 0.53499761988710f, 0.51410274419322f, 0.49289819222978f,
    0.47139673682600f, 0.44961132965461f, 0.42755509343028f, 0.40524131400499f,
    0.38268343236509f, 0.35989503653499f, 0.33688985339222f, 0.31368174039889f,
    0.29028467725446f, 0.26671275747490f, 0.24298017990326f, 0.21910124015687f,
    0.19509032201613f, 0.17096188876030f, 0.14673047445536f, 0.12241067519922f,
    0.09801714032956f, 0.07356456359967f, 0.04906767432742f, 0.02454122852291f};


