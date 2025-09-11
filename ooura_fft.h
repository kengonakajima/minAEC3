#ifndef MODULES_AUDIO_PROCESSING_UTILITY_OOURA_FFT_H_
#define MODULES_AUDIO_PROCESSING_UTILITY_OOURA_FFT_H_

 

struct OouraFft {
  OouraFft();
  ~OouraFft();
  void Fft(float* a) const;
  void InverseFft(float* a) const;

  void cft1st_128(float* a) const;
  void cftmdl_128(float* a) const;
  void rftfsub_128(float* a) const;
  void rftbsub_128(float* a) const;

  void cftfsub_128(float* a) const;
  void cftbsub_128(float* a) const;
  void bitrv2_128(float* a) const;
};

 

#endif  // MODULES_AUDIO_PROCESSING_UTILITY_OOURA_FFT_H_
