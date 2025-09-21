 

// 128ポイント実数FFTで生成された複素数データを保持する構造体。kFftLengthBy2Plus1は65。128ポイントの末尾63要素は対称なので不要
struct FftData {
  std::array<float, kFftLengthBy2Plus1> re; // 実数部分
  std::array<float, kFftLengthBy2Plus1> im; // 虚数部分
    
  // src内のデータをコピーする。
  void Assign(const FftData& src) {
    std::copy(src.re.begin(), src.re.end(), re.begin());
    std::copy(src.im.begin(), src.im.end(), im.begin());
    im[0] = im[kFftLengthBy2] = 0;
  }

  // 虚数部をすべてクリアする。
  void Clear() {
    re.fill(0.f);
    im.fill(0.f);
  }

  // データのパワースペクトルを計算する。
  void Spectrum(std::span<float> power_spectrum) const {
    std::transform(re.begin(), re.end(), im.begin(), power_spectrum.begin(),
                   [](float a, float b) { return a * a + b * b; });
  }

  // 配列入力からデータをコピーする。
  void CopyFromPackedArray(const std::array<float, kFftLength>& v) {
    re[0] = v[0];
    re[kFftLengthBy2] = v[1];
    im[0] = im[kFftLengthBy2] = 0;
    for (size_t k = 1, j = 2; k < kFftLengthBy2; ++k) {
      re[k] = v[j++];
      im[k] = v[j++];
    }
  }

  // データを配列へコピーする。
  void CopyToPackedArray(std::array<float, kFftLength>* v) const {
    
    (*v)[0] = re[0];
    (*v)[1] = re[kFftLengthBy2];
    for (size_t k = 1, j = 2; k < kFftLengthBy2; ++k) {
      (*v)[j++] = re[k];
      (*v)[j++] = im[k];
    }
  }
};

 

