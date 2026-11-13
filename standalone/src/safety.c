#include "safety.h"
#include <SDL.h>
#include <sys/thread.h>
#include <sys/process.h>
#include <unistd.h>
#include <stdio.h>
static SDL_atomic_t heartbeat,done,allowance,stage;
static sys_ppu_thread_t id;
static int started;
static void watch(void *unused) {
    (void)unused;
    int previous=SDL_AtomicGet(&heartbeat),ticks=0;
    while(!SDL_AtomicGet(&done)) {
        usleep(100000);
        int current=SDL_AtomicGet(&heartbeat);
        if(current!=previous) { previous=current; ticks=0; }
        else if(++ticks>=SDL_AtomicGet(&allowance)) {
            FILE *f=fopen("/dev_hdd0/game/SVIZ00001/USRDIR/safety-exit.log","w");
            if(f) { fprintf(f,"Watchdog: no main heartbeat. Stage %d (1 audio close, 2 PC disconnect, 3 update, 4 cleanup).\n",SDL_AtomicGet(&stage)); fclose(f); }
            sysProcessExit(15);
        }
    }
    sysThreadExit(0);
}
int safety_start(void) {
    SDL_AtomicSet(&done,0); SDL_AtomicSet(&allowance,100);
    started=sysThreadCreate(&id,watch,NULL,1000,32768,THREAD_JOINABLE,"neon_safety")==0;
    return started?0:-1;
}
void safety_beat(void) { SDL_AtomicAdd(&heartbeat,1); }
void safety_stage(int value) { SDL_AtomicSet(&stage,value); }
void safety_allow_seconds(int seconds) { SDL_AtomicSet(&allowance,seconds*10); safety_beat(); }
void safety_stop(void) {
    if(started) { u64 result; SDL_AtomicSet(&done,1); sysThreadJoin(id,&result); started=0; }
}
