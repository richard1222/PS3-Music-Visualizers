#ifndef NL_VISUALIZER_H
#define NL_VISUALIZER_H
#include "ui.h"
#include "spectrum.h"
#include <stdint.h>
void visualizer_draw(SDL_Surface *screen,const float bands[SV_BANDS],int64_t utc_seconds,Uint32 ticks);
#endif
