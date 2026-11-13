#ifndef SABER_SPECTRUM_H
#define SABER_SPECTRUM_H
#define SV_FFT 1024
#define SV_BANDS 32
typedef struct { float bands[SV_BANDS], rms, peak, bass; } Spectrum;
void spectrum_analyze(const float samples[SV_FFT], int rate, Spectrum *result);
#endif
