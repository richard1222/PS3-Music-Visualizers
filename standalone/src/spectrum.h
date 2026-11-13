#ifndef SABER_SPECTRUM_H
#define SABER_SPECTRUM_H
#define SV_FFT 1024
#define SV_BANDS 32
typedef struct { float bands[SV_BANDS], rms, peak, bass; } Spectrum;
typedef struct {
    int rate,first[SV_BANDS],last[SV_BANDS];
    float window[SV_FFT],step_r[10],step_i[10];
} SpectrumPlan;
void spectrum_plan_init(SpectrumPlan *plan,int rate);
void spectrum_analyze_plan(const float samples[SV_FFT],const SpectrumPlan *plan,Spectrum *result);
void spectrum_analyze(const float samples[SV_FFT], int rate, Spectrum *result);
#endif
