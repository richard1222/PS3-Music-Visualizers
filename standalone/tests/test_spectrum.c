#include "spectrum.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
    float samples[SV_FFT]={0}; Spectrum s; int i,maximum=0;
    spectrum_analyze(samples,48000,&s); assert(s.rms==0 && s.peak==0);
    for(i=0;i<SV_BANDS;i++)assert(s.bands[i]==0);
    for(i=0;i<SV_FFT;i++)samples[i]=0.5f*sinf(6.28318530718f*984.375f*i/48000);
    spectrum_analyze(samples,48000,&s);
    assert(s.rms>0.34f && s.rms<0.37f);
    for(i=1;i<SV_BANDS;i++)if(s.bands[i]>s.bands[maximum])maximum=i;
    assert(maximum>=16 && maximum<=18); assert(s.bands[maximum]>0.8f);
    SpectrumPlan plan; spectrum_plan_init(&plan,44100);
    for(i=0;i<SV_FFT;i++)samples[i]=0.5f*sinf(6.28318530718f*990*i/44100);
    for(int frame=0;frame<3;frame++) {
        spectrum_analyze_plan(samples,&plan,&s); maximum=0;
        for(i=1;i<SV_BANDS;i++)if(s.bands[i]>s.bands[maximum])maximum=i;
        assert(maximum>=16 && maximum<=18 && s.bands[maximum]>0.8f);
        assert(s.rms>0.34f && s.rms<0.37f);
    }
    for(i=0;i<SV_FFT;i++)samples[i]=NAN;
    spectrum_analyze(samples,48000,&s); assert(s.rms==0 && isfinite(s.bass));
    spectrum_analyze(samples,0,&s); assert(s.rms==0);
    puts("Spectrum: silence, calibrated tone, invalid samples and sample-rate checks passed");
    return 0;
}
