#include "display.h"
#include "runtime_ps3.h"
#ifdef NL_NATIVE_DISPLAY
#include <rsx/rsx.h>
#include <sysutil/video.h>
#include <sysutil/sysutil.h>
#include <sys/systime.h>
#include <sys/process.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static gcmContextData *context;
static void *io_memory,*pixels[2];
static SDL_Surface *canvas,*buffers[2];
static int current,pending,last_displayed=-1;
static u64 milliseconds(void) {
    u64 seconds,nanoseconds;
    if(sysGetCurrentTime(&seconds,&nanoseconds)) sysProcessExit(13);
    return seconds*1000+nanoseconds/1000000;
}
static int wait_for_flip(void) {
    u64 start=milliseconds();
    if(!pending) return 0;
    while(gcmGetFlipStatus()) {
        sysUtilCheckCallback();
        if(runtime_exit_requested() || milliseconds()-start>=2000) return -1;
        usleep(200);
    }
    gcmResetFlipStatus(); pending=0; return 0;
}
static void flush_pixels(void *address,size_t bytes) {
    uintptr_t first=(uintptr_t)address&~(uintptr_t)127;
    uintptr_t end=(uintptr_t)address+bytes;
    for(uintptr_t line=first;line<end;line+=128)
        __asm__ volatile("dcbst 0,%0"::"r"(line):"memory");
    __asm__ volatile("sync":::"memory");
}
Uint32 display_init_flags(void) { return 0; }
SDL_Surface *display_open(void) {
    videoState state; videoResolution resolution; videoConfiguration config;
    u32 offset; u64 start;
    if(videoGetState(VIDEO_PRIMARY,0,&state)) return NULL;
    if(videoGetResolution(state.displayMode.resolution,&resolution)) return NULL;
    if(!resolution.width || !resolution.height || resolution.width>1920 || resolution.height>1080) return NULL;
    io_memory=memalign(1024*1024,1024*1024);
    if(!io_memory) return NULL;
    context=NULL;
    if(rsxInit(&context,0x10000,1024*1024,io_memory) || !context) { context=NULL; return NULL; }
    memset(&config,0,sizeof(config));
    config.resolution=state.displayMode.resolution;
    config.aspect=state.displayMode.aspect;
    config.format=VIDEO_BUFFER_FORMAT_XRGB;
    config.pitch=resolution.width*4;
    if(videoConfigure(VIDEO_PRIMARY,&config,NULL,0)) return NULL;
    start=milliseconds();
    do {
        if(videoGetState(VIDEO_PRIMARY,0,&state)) return NULL;
        if(state.state!=3) break;
        sysUtilCheckCallback();
        if(runtime_exit_requested() || milliseconds()-start>=2000) return NULL;
        usleep(10000);
    } while(1);
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for(int i=0;i<2;i++) {
        pixels[i]=rsxMemalign(128,config.pitch*resolution.height);
        if(!pixels[i]) return NULL;
        memset(pixels[i],0,config.pitch*resolution.height);
        flush_pixels(pixels[i],config.pitch*resolution.height);
        buffers[i]=SDL_CreateRGBSurfaceFrom(pixels[i],resolution.width,resolution.height,32,config.pitch,0x00ff0000,0x0000ff00,0x000000ff,0xff000000);
        if(!buffers[i] || rsxAddressToOffset(pixels[i],&offset)) return NULL;
        if(gcmSetDisplayBuffer(i,offset,config.pitch,resolution.width,resolution.height)) return NULL;
    }
    canvas=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_ARGB8888);
    if(!canvas) return NULL;
    SDL_SetSurfaceBlendMode(canvas,SDL_BLENDMODE_NONE);
    gcmResetFlipStatus(); current=0; pending=0; last_displayed=-1;
    return canvas;
}
int display_present(void) {
    if(!canvas || !context || wait_for_flip()) return -1;
    if(SDL_BlitScaled(canvas,NULL,buffers[current],NULL)) return -1;
    flush_pixels(pixels[current],buffers[current]->pitch*buffers[current]->h);
    if(gcmSetFlip(context,current)) return -1;
    rsxFlushBuffer(context); pending=1; last_displayed=current; current=(current+1)%2;
    return 0;
}
int display_idle(void) {
    if(!context || wait_for_flip()) return -1;
    /* Preserve the last picture without CPU writes to either scanout buffer
     * while XMB owns drawing. Continue servicing display flips/callbacks. */
    if(last_displayed<0) return 0;
    if(gcmSetFlip(context,last_displayed)) return -1;
    rsxFlushBuffer(context); pending=1; return 0;
}
void display_close(void) {
    if(context) {
        wait_for_flip();
        rsxFinish(context,1);
    }
    for(int i=0;i<2;i++) {
        SDL_FreeSurface(buffers[i]); buffers[i]=NULL;
        if(pixels[i]) rsxFree(pixels[i]);
        pixels[i]=NULL;
    }
    SDL_FreeSurface(canvas); canvas=NULL;
    free(io_memory); io_memory=NULL; context=NULL;
}
#else
static SDL_Window *window;
Uint32 display_init_flags(void) { return SDL_INIT_VIDEO; }
SDL_Surface *display_open(void) {
    window=SDL_CreateWindow("Neon Lounge",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_FULLSCREEN);
    return window?SDL_GetWindowSurface(window):NULL;
}
int display_present(void) { return SDL_UpdateWindowSurface(window); }
int display_idle(void) { return 0; }
void display_close(void) { SDL_DestroyWindow(window); window=NULL; }
#endif
