// エコー減算器の出力特性を判定するためのヘルパー。
struct SubtractorOutputAnalyzer {
  bool filter_converged_ = false; // 直近期にフィルタが収束したかどうか
    
  void HandleEchoPathChange() { filter_converged_ = false; }  
  // 減算器出力から収束判定を行う。
  void Update(const SubtractorOutput& subtractor_output, bool* any_filter_converged) {
    *any_filter_converged = false;
    const float y2 = subtractor_output.y2;
    const float e2 = subtractor_output.e2;
    const float kConvergenceThreshold = 50 * 50 * static_cast<float>(kBlockSize);
    bool filter_converged_now = e2 < 0.5f * y2 && y2 > kConvergenceThreshold;
    filter_converged_ = filter_converged_now;
    *any_filter_converged = filter_converged_;
  }

};

