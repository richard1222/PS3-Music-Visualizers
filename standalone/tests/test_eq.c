#include "eq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static int fails;
#define CHECK(cond) do { if(!(cond)) { printf("FAIL line %d: %s\n",__LINE__,#cond); fails++; } } while(0)

/* Feed a stereo sine through eq_process in callback-sized blocks and
 * return the output/input RMS ratio measured over the final block. */
static double sine_ratio(double freq,int rate,int channels) {
    enum { BLOCK=2048, BLOCKS=24 };
    static int16_t buf[BLOCK];
    static int16_t before[BLOCK];
    double phase=0.0,rms_in=0.0,rms_out=0.0;
    for(int b=0;b<BLOCKS;b++) {
        for(int i=0;i<BLOCK;i+=channels) {
            int16_t v=(int16_t)(sin(phase)*10000.0);
            for(int c=0;c<channels;c++) buf[i+c]=v;
            phase+=2.0*M_PI*freq/(double)rate;
        }
        memcpy(before,buf,sizeof(buf));
        eq_process(buf,BLOCK,channels,rate);
        if(b==BLOCKS-1) {
            for(int i=0;i<BLOCK;i++) {
                rms_out+=(double)buf[i]*(double)buf[i];
                rms_in+=(double)before[i]*(double)before[i];
            }
        }
    }
    if(rms_in<=0.0) return 0.0;
    return sqrt(rms_out/rms_in);
}

int main(void) {
    /* Flat is a bit-exact pass-through. */
    eq_init();
    {
        int16_t buf[512],orig[512];
        for(int i=0;i<512;i++) buf[i]=orig[i]=(int16_t)((i*7919)%65536-32768);
        eq_process(buf,512,2,48000);
        CHECK(memcmp(buf,orig,sizeof(buf))==0);
        CHECK(eq_is_flat());
        CHECK(eq_match_preset()==0);
    }
    /* Preset bookkeeping. */
    eq_apply_preset(1);
    CHECK(eq_match_preset()==1);
    CHECK(eq_gain(0)==6 && eq_gain(1)==4 && eq_gain(2)==1 && eq_gain(4)==0);
    eq_set_gain(2,3);
    CHECK(eq_match_preset()==EQ_PRESET_CUSTOM);
    eq_set_gain(0,99);
    CHECK(eq_gain(0)==EQ_GAIN_MAX);
    eq_set_gain(0,-99);
    CHECK(eq_gain(0)==EQ_GAIN_MIN);
    eq_apply_preset(0);
    CHECK(eq_is_flat());
    /* A +12 dB mid boost lifts 1 kHz about 12 dB relative to 100 Hz
     * (the headroom preamp scales both equally). */
    eq_init();
    eq_set_gain(2,12);
    {
        double at_band=sine_ratio(1000.0,48000,2);
        double off_band=sine_ratio(100.0,48000,2);
        double rel=at_band/off_band;
        printf("mid +12: 1kHz %.3f, 100Hz %.3f, relative %.3f (want ~3.98)\n",at_band,off_band,rel);
        CHECK(rel>3.1 && rel<4.9);
    }
    /* A -12 dB mid cut drops 1 kHz to about a quarter amplitude. */
    eq_init();
    eq_set_gain(2,-12);
    {
        double r=sine_ratio(1000.0,48000,2);
        printf("mid -12: 1kHz %.3f (want ~0.251)\n",r);
        CHECK(r>0.18 && r<0.34);
    }
    /* Low shelf +12 lifts 60 Hz relative to 8 kHz. */
    eq_init();
    eq_set_gain(0,12);
    {
        double lo=sine_ratio(60.0,48000,2);
        double hi=sine_ratio(8000.0,48000,2);
        double rel=lo/hi;
        printf("shelf +12: 60Hz %.3f, 8kHz %.3f, relative %.3f (want ~3.98)\n",lo,hi,rel);
        CHECK(rel>3.0 && rel<5.0);
    }
    /* Mono path works too. */
    eq_init();
    eq_set_gain(2,12);
    {
        double at_band=sine_ratio(1000.0,44100,1);
        double off_band=sine_ratio(100.0,44100,1);
        double rel=at_band/off_band;
        printf("mono mid +12 @44.1k: relative %.3f (want ~3.98)\n",rel);
        CHECK(rel>3.1 && rel<4.9);
    }
    /* Full-scale noise through an all-+12 EQ stays bounded and alive
     * (no NaN blow-up, no railed-to-silence failure). */
    eq_init();
    for(int i=0;i<EQ_BANDS;i++) eq_set_gain(i,12);
    {
        static int16_t buf[4096];
        unsigned seed=12345;
        double energy=0.0;
        for(int b=0;b<12;b++) {
            for(int i=0;i<4096;i++) {
                seed=seed*1103515245u+12345u;
                buf[i]=(int16_t)((seed>>16)&0x7fff)-16384;
            }
            eq_process(buf,4096,2,48000);
            if(b==11) for(int i=0;i<4096;i++) energy+=(double)buf[i]*(double)buf[i];
        }
        printf("noise energy after all-+12: %.1f\n",energy);
        CHECK(energy>1.0e8);
    }
    /* A sample-rate switch mid-stream stays stable. */
    eq_init();
    eq_set_gain(0,8);
    {
        static int16_t buf[2048];
        for(int i=0;i<2048;i++) buf[i]=(int16_t)(sin(i*0.3)*12000.0);
        eq_process(buf,2048,2,48000);
        eq_process(buf,2048,2,22050);
        eq_process(buf,2048,2,8000);
        int lo=32767,hi=-32768;
        for(int i=0;i<2048;i++) { if(buf[i]<lo) lo=buf[i]; if(buf[i]>hi) hi=buf[i]; }
        CHECK(hi-lo>1000);
    }
    /* v1.18 user preset slots: capture, apply, match, persistence
     * line round-trip. Slots start unset; an unset slot never matches
     * and applying it is a no-op. */
    eq_init();
    CHECK(eq_match_user_preset()==-1);
    CHECK(!eq_user_preset_is_set(0));
    eq_set_gain(0,5); eq_set_gain(3,-3);
    eq_apply_user_preset(0);            /* unset: no-op */
    CHECK(eq_gain(0)==5 && eq_gain(3)==-3);
    eq_save_user_preset(1);
    CHECK(eq_user_preset_is_set(1));
    CHECK(eq_match_user_preset()==1);
    CHECK(eq_user_preset_last()==1);
    eq_apply_preset(0);                 /* flat */
    CHECK(eq_match_user_preset()==-1);
    eq_apply_user_preset(1);
    CHECK(eq_gain(0)==5 && eq_gain(1)==0 && eq_gain(3)==-3);
    CHECK(eq_match_user_preset()==1);
    CHECK(eq_match_preset()==EQ_PRESET_CUSTOM);
    /* Serialization: "slot <n> g0 g1 g2 g3 g4" round-trips, gains
     * clamp on parse, junk lines are refused. */
    {
        char line[96]; int slot=-1,g[EQ_BANDS];
        CHECK(eq_format_user_slot(line,sizeof(line),1)>0);
        CHECK(strcmp(line,"slot 1 5 0 0 -3 0")==0);
        CHECK(eq_format_user_slot(line,sizeof(line),0)==-1);   /* unset */
        CHECK(eq_parse_user_slot("slot 2 99 -99 1 2 3",&slot,g)==1);
        CHECK(slot==2 && g[0]==EQ_GAIN_MAX && g[1]==EQ_GAIN_MIN && g[4]==3);
        CHECK(eq_parse_user_slot("slot 7 0 0 0 0 0",&slot,g)==0);
        CHECK(eq_parse_user_slot("0 0 0 0 0",&slot,g)==0);
        CHECK(eq_parse_user_slot("slot 1 0 0",&slot,g)==0);
    }
    /* Load path: eq_user_preset_set marks the slot and clamps. */
    eq_init();
    {
        int g[EQ_BANDS]={1,2,3,4,50};
        eq_user_preset_set(2,g);
        CHECK(eq_user_preset_is_set(2));
        eq_apply_user_preset(2);
        CHECK(eq_gain(4)==EQ_GAIN_MAX);
        CHECK(eq_match_user_preset()==2);
    }
    if(fails) printf("test_eq: %d FAIL\n",fails);
    else printf("test_eq: PASS\n");
    return fails?1:0;
}
