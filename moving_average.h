#include <stddef.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <span>

 

struct MovingAverage {
  // Creates an instance of MovingAverage that accepts inputs of length num_elem
  // and averages over mem_len inputs.
  MovingAverage(size_t num_elem, size_t mem_len)
      : num_elem_(num_elem),
        mem_len_(mem_len - 1),
        scaling_(1.0f / static_cast<float>(mem_len)),
        memory_(num_elem * mem_len_, 0.f),
        mem_index_(0) {}
  

  // Computes the average of input and mem_len-1 previous inputs and stores the
  // result in output.
  void Average(std::span<const float> input, std::span<float> output) {
    std::copy(input.begin(), input.end(), output.begin());
    for (auto i = memory_.begin(); i < memory_.end(); i += num_elem_) {
      std::transform(i, i + num_elem_, output.begin(), output.begin(),
                     std::plus<float>());
    }
    for (float& o : output) {
      o *= scaling_;
    }
    if (mem_len_ > 0) {
      std::copy(input.begin(), input.end(),
                memory_.begin() + mem_index_ * num_elem_);
      mem_index_ = (mem_index_ + 1) % mem_len_;
    }
  }

  const size_t num_elem_;
  const size_t mem_len_;
  const float scaling_;
  std::vector<float> memory_;
  size_t mem_index_;
};

