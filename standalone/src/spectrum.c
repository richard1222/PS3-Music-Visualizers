#include "spectrum.h"
#include <math.h>
#include <string.h>
static float clamp(float x) { return x<0?0:(x>1?1:x); }
void spectrum_analyze(const float samples[SV_FFT], int rate, Spectrum *out) {
    float re[SV_FFT],im[SV_FFT],power[SV_FFT/2];
    double sum=0; int i,j=0,length;
    memset(out,0,sizeof(*out)); if(rate<8000 || rate>192000) return;
    for(i=0;i<SV_FFT;i++) {
        float x=isfinite(samples[i])?samples[i]:0;
        if(x>1)x=1;
        if(x<-1)x=-1;
        sum+=x*x; if(fabsf(x)>out->peak)out->peak=fabsf(x);
        re[i]=x*(0.5f-0.5f*cosf(6.28318530718f*i/(SV_FFT-1))); im[i]=0;
    }
    out->rms=(float)sqrt(sum/SV_FFT);
    for(i=1;i<SV_FFT;i++) {
        int bit=SV_FFT>>1; float t;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j) {t=re[i];re[i]=re[j];re[j]=t;}
    }
    for(length=2;length<=SV_FFT;length<<=1) {
        float angle=-6.28318530718f/length;
        float step_r=cosf(angle),step_i=sinf(angle);
        for(i=0;i<SV_FFT;i+=length) {
            float wr=1,wi=0;
            for(j=0;j<length/2;j++) {
                int a=i+j,b=a+length/2;
                float tr=wr*re[b]-wi*im[b],ti=wr*im[b]+wi*re[b];
                float nr=wr*step_r-wi*step_i;
                re[b]=re[a]-tr;im[b]=im[a]-ti;re[a]+=tr;im[a]+=ti;
                wi=wr*step_i+wi*step_r;wr=nr;
            }
        }
    }
    for(i=0;i<SV_FFT/2;i++)power[i]=4*sqrtf(re[i]*re[i]+im[i]*im[i])/SV_FFT;
    for(i=0;i<SV_BANDS;i++) {
        float low=40*powf(400.0f,(float)i/SV_BANDS);
        float high=40*powf(400.0f,(float)(i+1)/SV_BANDS),amplitude=0;
        int first=(int)(low*SV_FFT/rate),last=(int)(high*SV_FFT/rate);
        if(first<1)first=1;
        if(last<=first)last=first+1;
        if(last>SV_FFT/2)last=SV_FFT/2;
        for(j=first;j<last;j++)if(power[j]>amplitude)amplitude=power[j];
        out->bands[i]=amplitude>0.00001f?clamp((20*log10f(amplitude)+65)/65):0;
    }
    for(i=0;i<7;i++)out->bass+=out->bands[i]/7;
}
