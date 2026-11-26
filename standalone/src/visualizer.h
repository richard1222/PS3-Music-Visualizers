#ifndef NL_VISUALIZER_H
#define NL_VISUALIZER_H
#include "ui.h"
#include "spectrum.h"
#include <stdint.h>
/* Visualizer modes (v1.18). The draw entry point takes the whole
 * Spectrum (bands + rms/peak/bass) so modes can use the bass and
 * peak data spectrum.c already computes. Mode switching lives in
 * main.c (viz_mode, Triangle in the Visualizer view, persisted to
 * USRDIR/viz.txt); this file only draws. All modes keep primitive
 * counts in the low thousands - the frame watchdog exits the app
 * after 10 s below 10 FPS. */
enum { VIZ_BARS=0, VIZ_PEAK_RAIN=1, VIZ_JEWEL=2, VIZ_SUNBURST=3, VIZ_WAVEFORM=4, VIZ_MODE_COUNT=5 };
const char *visualizer_mode_name(int mode);
/* The waveform mode draws raw PCM, which the Spectrum does not
 * carry: main hands the latest snapshot over before drawing
 * (the samples are copied, so the caller's buffer may be reused). */
void visualizer_set_waveform(const float *samples,int count);
void visualizer_draw(SDL_Surface *screen,const Spectrum *spectrum,int mode,int64_t utc_seconds,Uint32 ticks);
#endif
