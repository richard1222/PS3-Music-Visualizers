#include "visualizer.h"
#include "clock_est.h"
#include <math.h>
#include <stdio.h>
/* OG Xbox theme greens: deep panel, ring green, jewel, pale glow. */
#define VIZ_PANEL   0x0d1206
#define VIZ_BASE    0x3d5a1a
#define VIZ_JEWEL   0x76bc21
#define VIZ_ACCENT  0x9ede3c
#define VIZ_PALE    0xd9ffb3

const char *visualizer_mode_name(int mode) {
    static const char *names[VIZ_MODE_COUNT]={"Spectrum Bars","Peak Rain","Jewel Pulse","Radial Sunburst","Waveform"};
    if(mode<0 || mode>=VIZ_MODE_COUNT) mode=VIZ_BARS;
    return names[mode];
}

static float wave_samples[1024];
static int wave_count;
void visualizer_set_waveform(const float *samples,int count) {
    if(!samples || count<=0) { wave_count=0; return; }
    if(count>1024) count=1024;
    for(int i=0;i<count;i++) {
        float v=samples[i];
        if(!isfinite(v)) v=0;
        if(v>1) v=1; if(v<-1) v=-1;
        wave_samples[i]=v;
    }
    wave_count=count;
}
static void draw_waveform(SDL_Surface *screen,const Spectrum *sp) {
    /* The raw PCM trace across the middle of the screen, oscilloscope
     * style: a center line, the trace in jewel green, and a pale
     * mirror of the loudest excursion per column so transients read
     * at a glance. The bottom strip carries a simple level bar. */
    const int x0=44,x1=1236,cy=320,amp=140;
    ui_rect(screen,32,150,1216,360,VIZ_PANEL);
    ui_line(screen,x0,cy,x1,cy,VIZ_BASE);
    if(wave_count>1) {
        const int cols=298;
        int step=wave_count/cols;
        if(step<1) step=1;
        int prevx=x0,prevy=cy+(int)(wave_samples[0]*(float)amp);
        for(int i=1;i<cols;i++) {
            int idx=i*step;
            if(idx>=wave_count) idx=wave_count-1;
            int x=x0+i*(x1-x0)/cols;
            int y=cy+(int)(wave_samples[idx]*(float)amp);
            ui_line(screen,prevx,prevy,x,y,VIZ_ACCENT);
            prevx=x; prevy=y;
        }
    }
    ui_rect(screen,32,524,1216,80,VIZ_PANEL);
    float rms=sp->rms;
    if(!isfinite(rms) || rms<0) rms=0;
    if(rms>1) rms=1;
    ui_rect(screen,44,556,1192,16,VIZ_BASE);
    ui_rect(screen,44,556,(int)(1192*rms),16,VIZ_JEWEL);
    ui_text(screen,48,540,18,VIZ_ACCENT,"Level");
}

static float band_value(const Spectrum *sp,int i) {
    float v=sp->bands[i&(SV_BANDS-1)];
    if(!isfinite(v) || v<0) v=0;
    if(v>1) v=1;
    return v;
}
/* Bar gradient: dark root to bright jewel tip, i across 64 columns. */
static Uint32 bar_color(int i) {
    unsigned r=28+(unsigned)(i*0.5f);
    unsigned g=110+i*2; if(g>235) g=235;
    unsigned b=34;
    return (r<<16)|(g<<8)|b;
}

static void draw_clock(SDL_Surface *screen,int64_t utc_seconds) {
    char clock[20]; clock_est_format(utc_seconds,clock,sizeof(clock));
    ui_rect(screen,920,16,328,86,VIZ_PANEL);
    ui_text(screen,940,22,42,0xf2f7ff,clock);
    ui_text(screen,940,72,18,VIZ_ACCENT,"EST / UTC-5");
}

static void draw_bars(SDL_Surface *screen,const Spectrum *sp,int peaks,Uint32 ticks) {
    static float caps[64];
    static Uint32 caps_last;
    ui_rect(screen,32,524,1216,80,VIZ_PANEL);
    ui_rect(screen,42,563,1196,2,VIZ_BASE);
    if(peaks) {
        /* Falling peak caps: gravity in bar-length units per second,
         * integrated from the real tick delta so the fall rate is
         * frame-rate independent. Caps only ever sit on a bar top. */
        float step=0.0f;
        if(caps_last) step=(float)(ticks-caps_last)/1000.0f*22.0f;
        caps_last=ticks;
        if(step<0) step=0; if(step>4) step=4;
        for(int i=0;i<64;i++) {
            int length=1+(int)(band_value(sp,i<32?i:63-i)*34);
            if((float)length>caps[i]) caps[i]=(float)length;
            else {
                caps[i]-=step;
                if(caps[i]<(float)length) caps[i]=(float)length;
                if(caps[i]<1) caps[i]=1;
            }
        }
    }
    for(int i=0;i<64;i++) {
        int length=1+(int)(band_value(sp,i<32?i:63-i)*34);
        ui_rect(screen,44+i*18,564-length,12,length*2,bar_color(i));
        if(peaks) {
            int cap=(int)(caps[i]+0.5f);
            if(cap<1) cap=1; if(cap>35) cap=35;
            ui_rect(screen,44+i*18,564-cap,12,3,VIZ_PALE);
        }
    }
}

static void draw_jewel(SDL_Surface *screen,const Spectrum *sp,Uint32 ticks) {
    /* Concentric diamond outlines pulsing on the bass band. A slow
     * breathing term keeps the jewel alive in quiet passages. */
    float bass=sp->bass;
    if(!isfinite(bass) || bass<0) bass=0;
    if(bass>1) bass=1;
    float breathe=0.06f*sinf((float)ticks*0.0016f);
    float r=46.0f+bass*150.0f+breathe*160.0f;
    if(r<24) r=24;
    const int cx=640,cy=330;
    static const float scale[4]={1.0f,0.74f,0.5f,0.27f};
    static const Uint32 ring[4]={VIZ_BASE,VIZ_JEWEL,VIZ_ACCENT,VIZ_PALE};
    ui_rect(screen,32,524,1216,80,VIZ_PANEL);
    for(int k=0;k<4;k++) {
        int rr=(int)(r*scale[k]);
        ui_line(screen,cx,cy-rr,cx+rr,cy,ring[k]);
        ui_line(screen,cx+rr,cy,cx,cy+rr,ring[k]);
        ui_line(screen,cx,cy+rr,cx-rr,cy,ring[k]);
        ui_line(screen,cx-rr,cy,cx,cy-rr,ring[k]);
    }
    int core=4+(int)(bass*10.0f);
    ui_rect(screen,cx-core,cy-core,core*2,core*2,VIZ_JEWEL);
    char label[64];
    snprintf(label,sizeof(label),"Bass %d%%",(int)(bass*100.0f+0.5f));
    ui_text(screen,48,540,18,VIZ_ACCENT,label);
}

static void draw_sunburst(SDL_Surface *screen,const Spectrum *sp) {
    /* 32 rays from screen center, one per spectrum band. */
    const int cx=640,cy=330;
    ui_rect(screen,32,524,1216,80,VIZ_PANEL);
    for(int i=0;i<SV_BANDS;i++) {
        float v=band_value(sp,i);
        float a=(float)i*(6.28318530718f/SV_BANDS)-1.57079632679f;
        int len=34+(int)(v*216.0f);
        int x1=cx+(int)(cosf(a)*34.0f),y1=cy+(int)(sinf(a)*34.0f);
        int x2=cx+(int)(cosf(a)*(float)len),y2=cy+(int)(sinf(a)*(float)len);
        unsigned g=110+(unsigned)(v*140.0f); if(g>255) g=255;
        Uint32 color=(40u<<16)|(g<<8)|34u;
        ui_line(screen,x1,y1,x2,y2,color);
    }
    ui_rect(screen,cx-5,cy-5,10,10,VIZ_JEWEL);
}

void visualizer_draw(SDL_Surface *screen,const Spectrum *spectrum,int mode,int64_t utc_seconds,Uint32 ticks) {
    if(!spectrum) return;
    switch(mode) {
        case VIZ_PEAK_RAIN: draw_bars(screen,spectrum,1,ticks); break;
        case VIZ_JEWEL:     draw_jewel(screen,spectrum,ticks); break;
        case VIZ_SUNBURST:  draw_sunburst(screen,spectrum); break;
        case VIZ_WAVEFORM:  draw_waveform(screen,spectrum); break;
        case VIZ_BARS:
        default:            draw_bars(screen,spectrum,0,ticks); break;
    }
    draw_clock(screen,utc_seconds);
}
