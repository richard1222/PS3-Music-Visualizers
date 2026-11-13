#ifndef NL_FRAME_HEALTH_H
#define NL_FRAME_HEALTH_H
#include <stdint.h>
typedef struct { uint32_t slow_ms; } FrameHealth;
/* Quit only after ten uninterrupted seconds below 10 FPS. */
static inline int frame_health_update(FrameHealth *h,uint32_t frame_ms) {
    if(frame_ms<100) h->slow_ms=0;
    else if(h->slow_ms<10000) h->slow_ms+=frame_ms>10000?10000:frame_ms;
    return h->slow_ms>=10000;
}
#endif
