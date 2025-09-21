#include <stddef.h>
#include <array>
#include <vector>
#include <algorithm>
#include <functional>
#include <span>

 
// Computes and stores the frequency response of the filter.
inline void ComputeFrequencyResponse(
    size_t num_partitions,
    const std::vector<FftData>& H,
    std::vector<std::array<float, kFftLengthBy2Plus1>>* H2) {
  for (auto& H2_ch : *H2) {
    H2_ch.fill(0.f);
  }
  for (size_t p = 0; p < num_partitions; ++p) {
    for (size_t j = 0; j < kFftLengthBy2Plus1; ++j) {
      float tmp = H[p].re[j] * H[p].re[j] + H[p].im[j] * H[p].im[j];
      (*H2)[p][j] = std::max((*H2)[p][j], tmp);
    }
  }
}

// Adapts the filter partitions.
inline void AdaptPartitions(const RenderBuffer& render_buffer,
                            const FftData& G,
                            size_t num_partitions,
                            std::vector<FftData>* H) {
  std::span<const FftData> render_buffer_data = render_buffer.GetFftBuffer();
  size_t index = render_buffer.Position();
  for (size_t p = 0; p < num_partitions; ++p) {
    const FftData& X_p = render_buffer_data[index];
    FftData& H_p = (*H)[p];
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      H_p.re[k] += X_p.re[k] * G.re[k] + X_p.im[k] * G.im[k];
      H_p.im[k] += X_p.re[k] * G.im[k] - X_p.im[k] * G.re[k];
    }
    index = index < (render_buffer_data.size() - 1) ? index + 1 : 0;
  }
}

// Produces the filter output.
inline void ApplyFilter(const RenderBuffer& render_buffer,
                        size_t num_partitions,
                        const std::vector<FftData>& H,
                        FftData* S) {
  S->re.fill(0.f);
  S->im.fill(0.f);
  std::span<const FftData> render_buffer_data = render_buffer.GetFftBuffer();
  size_t index = render_buffer.Position();
  for (size_t p = 0; p < num_partitions; ++p) {
    const FftData& X_p = render_buffer_data[index];
    const FftData& H_p = H[p];
    for (size_t k = 0; k < kFftLengthBy2Plus1; ++k) {
      S->re[k] += X_p.re[k] * H_p.re[k] - X_p.im[k] * H_p.im[k];
      S->im[k] += X_p.re[k] * H_p.im[k] + X_p.im[k] * H_p.re[k];
    }
    index = index < (render_buffer_data.size() - 1) ? index + 1 : 0;
  }
}

// Computes the echo return loss based on a frequency response.
inline void ComputeErl(
    const std::vector<std::array<float, kFftLengthBy2Plus1>>& H2,
    std::span<float> erl) {
  std::fill(erl.begin(), erl.end(), 0.f);
  for (auto& H2_j : H2) {
    std::transform(H2_j.begin(), H2_j.end(), erl.begin(), erl.begin(),
                   std::plus<float>());
  }
}

 

// Provides a frequency domain adaptive filter functionality.
struct AdaptiveFirFilter {
  AdaptiveFirFilter(size_t size_partitions)
      : fft_(), size_partitions_(size_partitions), H_(size_partitions) {
    for (size_t p = 0; p < H_.size(); ++p) {
      H_[p].Clear();
    }
  }

  


  // Produces the output of the filter.
  void Filter(const RenderBuffer& render_buffer, FftData* S) const {
    ::ApplyFilter(render_buffer, size_partitions_, H_, S);
  }

  // Adapts the filter.
  void Adapt(const RenderBuffer& render_buffer, const FftData& G) {
    ::AdaptPartitions(render_buffer, G, size_partitions_, &H_);
    Constrain();
  }

  // Receives reports that known echo path changes have occured and adjusts
  // the filter adaptation accordingly.
  void HandleEchoPathChange() {
    for (size_t p = 0; p < H_.size(); ++p) {
      H_[p].Clear();
    }
  }

  // Returns the filter size (固定)。
  size_t SizePartitions() const { return size_partitions_; }

  // Computes the frequency responses for the filter partitions.
  void ComputeFrequencyResponse(
      std::vector<std::array<float, kFftLengthBy2Plus1>>* H2) const {
    H2->resize(size_partitions_);
    ::ComputeFrequencyResponse(size_partitions_, H_, H2);
  }

  // Adapts the filter and updates the filter size.
  //（サイズは固定化済みのため内側で直接更新します）

  // Constrain the filter partitions in a cyclic manner.
  void Constrain() {
    std::array<float, kFftLength> h;
    {
      fft_.Ifft(H_[partition_to_constrain_], &h);
      static const float kScale = 1.0f / static_cast<float>(kFftLengthBy2);
      std::for_each(h.begin(), h.begin() + kFftLengthBy2,
                    [](float& a) { a *= kScale; });
      std::fill(h.begin() + kFftLengthBy2, h.end(), 0.f);
      fft_.Fft(&h, &H_[partition_to_constrain_]);
    }
    partition_to_constrain_ =
        partition_to_constrain_ < (size_partitions_ - 1)
            ? partition_to_constrain_ + 1
            : 0;
  }

  
  const Aec3Fft fft_;
  const size_t size_partitions_;
  std::vector<FftData> H_;
  size_t partition_to_constrain_ = 0;
};
