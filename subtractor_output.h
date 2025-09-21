
// Stores the values being returned from the echo subtractor for a single
// capture channel.
struct SubtractorOutput {
  SubtractorOutput() = default;
  

  std::array<float, kBlockSize> s;
  std::array<float, kBlockSize> e;
  FftData E;
  std::array<float, kFftLengthBy2Plus1> E2;
  float e2 = 0.f;
  float y2 = 0.f;

  // Updates the powers of the signals.
  void ComputeMetrics(std::span<const float> y) {
    const auto sum_of_squares = [](float a, float b) { return a + b * b; };
    y2 = std::accumulate(y.begin(), y.end(), 0.f, sum_of_squares);
    e2 = std::accumulate(e.begin(), e.end(), 0.f, sum_of_squares);
  }
};

 

