#include <stddef.h>
#include <array>
#include <span>
 

// Provides a buffer of the render data for the echo remover.
struct RenderBuffer {
  RenderBuffer(BlockBuffer* block_buffer,
               SpectrumBuffer* spectrum_buffer,
               FftBuffer* fft_buffer)
      : block_buffer_(block_buffer),
        spectrum_buffer_(spectrum_buffer),
        fft_buffer_(fft_buffer) {}


  ~RenderBuffer() = default;

  // Get a block.
  const Block& GetBlock(int buffer_offset_blocks) const {
    int position =
        block_buffer_->OffsetIndex(block_buffer_->read, buffer_offset_blocks);
    return block_buffer_->buffer[position];
  }

  const std::array<float, kFftLengthBy2Plus1>& Spectrum(
      int buffer_offset_ffts) const {
    int position = spectrum_buffer_->OffsetIndex(spectrum_buffer_->read,
                                                 buffer_offset_ffts);
    return spectrum_buffer_->buffer[position];
  }

  // Returns the circular fft buffer (mono).
  std::span<const FftData> GetFftBuffer() const { return fft_buffer_->buffer; }

  // Returns the current position in the circular buffer.
  size_t Position() const {
    
    
    return fft_buffer_->read;
  }

  // Returns the sum of the spectrums for a certain number of FFTs.
  void SpectralSum(size_t num_spectra,
                   std::array<float, kFftLengthBy2Plus1>* X2) const {
    X2->fill(0.f);
    int position = spectrum_buffer_->read;
    for (size_t j = 0; j < num_spectra; ++j) {
      const auto& spectrum = spectrum_buffer_->buffer[position];
      for (size_t k = 0; k < X2->size(); ++k) {
        (*X2)[k] += spectrum[k];
      }
      position = spectrum_buffer_->IncIndex(position);
    }
  }

  // Returns a reference to the spectrum buffer.
  const SpectrumBuffer& GetSpectrumBuffer() const { return *spectrum_buffer_; }

  const BlockBuffer* const block_buffer_;
  const SpectrumBuffer* const spectrum_buffer_;
  const FftBuffer* const fft_buffer_;
};


