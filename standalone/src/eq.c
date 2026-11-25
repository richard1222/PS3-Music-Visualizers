#include "eq.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const int eq_band_freq[EQ_BANDS]={100,400,1000,4000,12000};
const char *const eq_band_name[EQ_BANDS]={"Bass","Low Mid","Mid","High Mid","Treble"};
const char *const eq_preset_name[EQ_PRESET_COUNT]={"Flat","Bass Boost","Treble Boost","Vocal","Bright","Warm"};
static const int eq_presets[EQ_PRESET_COUNT][EQ_BANDS]={
    {0,0,0,0,0},        /* Flat */
    {6,4,1,0,0},        /* Bass Boost */
    {0,0,1,4,6},        /* Treble Boost */
    {-2,2,4,3,0},       /* Vocal */
    {-1,0,1,4,7},       /* Bright */
    {4,3,0,-2,-4}       /* Warm */
};
const char *const eq_user_preset_name[EQ_USER_PRESET_COUNT]={"Custom 1","Custom 2","Custom 3"};
static int user_presets[EQ_USER_PRESET_COUNT][EQ_BANDS];
static int user_set[EQ_USER_PRESET_COUNT];
static int user_last=-1;
enum { BAND_LOWSHELF,BAND_PEAK,BAND_PEAK2,BAND_PEAK3,BAND_HIGHSHELF };
static const int eq_band_type[EQ_BANDS]={BAND_LOWSHELF,BAND_PEAK,BAND_PEAK,BAND_PEAK,BAND_HIGHSHELF};

static int gains[EQ_BANDS];
/* Audio-thread coefficient cache: rebuilt when gains or rate change. */
static int built_rate;
static int built_gains[EQ_BANDS];
static float coef[EQ_BANDS][5]; /* b0,b1,b2,a1,a2 (a0 normalized to 1) */
static int band_active[EQ_BANDS];
static float preamp;
static float state[EQ_BANDS][2][2]; /* DF2T z1,z2 per band per channel */

static void build_band(int band,int rate) {
    int db=gains[band];
    band_active[band]=(db!=0);
    if(!band_active[band]) return;
    double fs=(double)rate;
    double f=(double)eq_band_freq[band];
    double fmax=fs*0.42;
    if(f>fmax) f=fmax;
    if(f<10.0) f=10.0;
    double a=pow(10.0,(double)db/40.0);
    double w=2.0*M_PI*f/fs;
    double cw=cos(w),sw=sin(w);
    double b0,b1,b2,a0,a1,a2;
    if(eq_band_type[band]==BAND_PEAK) {
        double alpha=sw/2.0; /* Q = 1 */
        b0=1.0+alpha*a; b1=-2.0*cw; b2=1.0-alpha*a;
        a0=1.0+alpha/a; a1=-2.0*cw; a2=1.0-alpha/a;
    } else {
        double alpha=sw/2.0*sqrt(2.0); /* shelf slope S = 1 */
        double sq=2.0*sqrt(a)*alpha;
        if(eq_band_type[band]==BAND_LOWSHELF) {
            b0=a*((a+1.0)-(a-1.0)*cw+sq);
            b1=2.0*a*((a-1.0)-(a+1.0)*cw);
            b2=a*((a+1.0)-(a-1.0)*cw-sq);
            a0=(a+1.0)+(a-1.0)*cw+sq;
            a1=-2.0*((a-1.0)+(a+1.0)*cw);
            a2=(a+1.0)+(a-1.0)*cw-sq;
        } else {
            b0=a*((a+1.0)+(a-1.0)*cw+sq);
            b1=-2.0*a*((a-1.0)+(a+1.0)*cw);
            b2=a*((a+1.0)+(a-1.0)*cw-sq);
            a0=(a+1.0)-(a-1.0)*cw+sq;
            a1=2.0*((a-1.0)-(a+1.0)*cw);
            a2=(a+1.0)-(a-1.0)*cw-sq;
        }
    }
    coef[band][0]=(float)(b0/a0);
    coef[band][1]=(float)(b1/a0);
    coef[band][2]=(float)(b2/a0);
    coef[band][3]=(float)(a1/a0);
    coef[band][4]=(float)(a2/a0);
}

static void rebuild(int rate) {
    int max_boost=0;
    for(int i=0;i<EQ_BANDS;i++) {
        build_band(i,rate);
        if(gains[i]>max_boost) max_boost=gains[i];
        built_gains[i]=gains[i];
    }
    built_rate=rate;
    preamp=(float)pow(10.0,-(double)max_boost/20.0);
}

void eq_init(void) {
    memset(gains,0,sizeof(gains));
    memset(built_gains,0,sizeof(built_gains));
    memset(state,0,sizeof(state));
    memset(user_presets,0,sizeof(user_presets));
    memset(user_set,0,sizeof(user_set));
    user_last=-1;
    built_rate=0;
    preamp=1.0f;
}

void eq_set_gain(int band,int db) {
    if(band<0 || band>=EQ_BANDS) return;
    if(db<EQ_GAIN_MIN) db=EQ_GAIN_MIN;
    if(db>EQ_GAIN_MAX) db=EQ_GAIN_MAX;
    gains[band]=db;
}

int eq_gain(int band) {
    if(band<0 || band>=EQ_BANDS) return 0;
    return gains[band];
}

void eq_apply_preset(int preset) {
    if(preset<0 || preset>=EQ_PRESET_COUNT) return;
    for(int i=0;i<EQ_BANDS;i++) gains[i]=eq_presets[preset][i];
}

int eq_match_preset(void) {
    for(int p=0;p<EQ_PRESET_COUNT;p++) {
        int i;
        for(i=0;i<EQ_BANDS;i++) if(gains[i]!=eq_presets[p][i]) break;
        if(i==EQ_BANDS) return p;
    }
    return EQ_PRESET_CUSTOM;
}

int eq_match_user_preset(void) {
    for(int s=0;s<EQ_USER_PRESET_COUNT;s++) {
        int i;
        if(!user_set[s]) continue;
        for(i=0;i<EQ_BANDS;i++) if(gains[i]!=user_presets[s][i]) break;
        if(i==EQ_BANDS) return s;
    }
    return -1;
}

void eq_apply_user_preset(int slot) {
    if(slot<0 || slot>=EQ_USER_PRESET_COUNT || !user_set[slot]) return;
    for(int i=0;i<EQ_BANDS;i++) gains[i]=user_presets[slot][i];
    user_last=slot;
}

void eq_save_user_preset(int slot) {
    if(slot<0 || slot>=EQ_USER_PRESET_COUNT) return;
    for(int i=0;i<EQ_BANDS;i++) user_presets[slot][i]=gains[i];
    user_set[slot]=1;
    user_last=slot;
}

void eq_user_preset_set(int slot,const int gains_in[EQ_BANDS]) {
    if(slot<0 || slot>=EQ_USER_PRESET_COUNT || !gains_in) return;
    for(int i=0;i<EQ_BANDS;i++) {
        int g=gains_in[i];
        if(g<EQ_GAIN_MIN) g=EQ_GAIN_MIN;
        if(g>EQ_GAIN_MAX) g=EQ_GAIN_MAX;
        user_presets[slot][i]=g;
    }
    user_set[slot]=1;
}

int eq_user_preset_is_set(int slot) {
    if(slot<0 || slot>=EQ_USER_PRESET_COUNT) return 0;
    return user_set[slot];
}

int eq_user_preset_last(void) { return user_last; }

int eq_format_user_slot(char *buf,size_t size,int slot) {
    if(!buf || slot<0 || slot>=EQ_USER_PRESET_COUNT || !user_set[slot]) return -1;
    int n=snprintf(buf,size,"slot %d %d %d %d %d %d",slot,
        user_presets[slot][0],user_presets[slot][1],user_presets[slot][2],
        user_presets[slot][3],user_presets[slot][4]);
    if(n<0 || (size_t)n>=size) return -1;
    return n;
}

int eq_parse_user_slot(const char *line,int *slot_out,int gains_out[EQ_BANDS]) {
    int slot,g[EQ_BANDS];
    if(!line || !slot_out || !gains_out) return 0;
    if(sscanf(line,"slot %d %d %d %d %d %d",&slot,&g[0],&g[1],&g[2],&g[3],&g[4])!=6) return 0;
    if(slot<0 || slot>=EQ_USER_PRESET_COUNT) return 0;
    for(int i=0;i<EQ_BANDS;i++) {
        if(g[i]<EQ_GAIN_MIN) g[i]=EQ_GAIN_MIN;
        if(g[i]>EQ_GAIN_MAX) g[i]=EQ_GAIN_MAX;
        gains_out[i]=g[i];
    }
    *slot_out=slot;
    return 1;
}

int eq_is_flat(void) {
    for(int i=0;i<EQ_BANDS;i++) if(gains[i]) return 0;
    return 1;
}

void eq_process(int16_t *samples,size_t count,int channels,int sample_rate) {
    if(!samples || !count || sample_rate<1000) return;
    if(eq_is_flat()) return;
    if(channels<1) channels=1;
    if(channels>2) channels=2;
    if(sample_rate!=built_rate) { rebuild(sample_rate); memset(state,0,sizeof(state)); }
    else {
        for(int i=0;i<EQ_BANDS;i++) if(gains[i]!=built_gains[i]) { rebuild(sample_rate); break; }
    }
    size_t frames=count/(size_t)channels;
    for(size_t n=0;n<frames;n++) {
        for(int c=0;c<channels;c++) {
            float x=(float)samples[n*(size_t)channels+(size_t)c]*preamp;
            for(int b=0;b<EQ_BANDS;b++) {
                if(!band_active[b]) continue;
                float y=coef[b][0]*x+state[b][c][0];
                state[b][c][0]=coef[b][1]*x-coef[b][3]*y+state[b][c][1];
                state[b][c][1]=coef[b][2]*x-coef[b][4]*y;
                x=y;
            }
            if(x>32767.0f) x=32767.0f;
            else if(x<-32768.0f) x=-32768.0f;
            samples[n*(size_t)channels+(size_t)c]=(int16_t)lrintf(x);
        }
    }
}
