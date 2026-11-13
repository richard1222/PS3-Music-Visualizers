#include "visualizer.h"
#include "clock_est.h"
#include <math.h>
void visualizer_draw(SDL_Surface *screen,const float bands[SV_BANDS],int64_t utc_seconds,Uint32 ticks) {
    (void)ticks;
    /* Original native straight spectrum; driven only by decoded PCM bands. */
    ui_rect(screen,32,514,1216,80,0x0b1524);
    ui_rect(screen,42,553,1196,2,0x24485d);
    for(int i=0;i<64;i++) {
        float value=bands[i<32?i:63-i];
        if(!isfinite(value) || value<0) value=0;
        if(value>1) value=1;
        int length=1+(int)(value*34);
        unsigned r=40+i*3,g=220-i*2,b=235;
        Uint32 color=(r<<16)|(g<<8)|b;
        ui_rect(screen,44+i*18,554-length,12,length*2,color);
    }
    char clock[20];clock_est_format(utc_seconds,clock,sizeof(clock));
    ui_rect(screen,920,16,328,86,0x0b1524);
    ui_text(screen,940,22,42,0xf2f7ff,clock);
    ui_text(screen,940,72,18,0x35dfeb,"EST / UTC-5");
}
