#include "runtime_ps3.h"
#include "wait_guard.h"
#include "system_state.h"
#include "thread_stack.h"
#include "youtube.h"   /* yt_log_sink - the OSK session event trace */
#include <SDL.h>
#include <sysutil/sysutil.h>
#include <sys/systime.h>
#include <sys/process.h>
#include <sys/thread.h>
#include <stdio.h>
extern s32 __real_sysThreadCreate(sys_ppu_thread_t *,void (*)(void *),void *,s32,u64,u64,char *);
s32 __wrap_sysThreadCreate(sys_ppu_thread_t *id,void (*entry)(void *),void *arg,s32 priority,u64 stack,u64 flags,char *name) {
    return __real_sysThreadCreate(id,entry,arg,priority,thread_stack_bytes(name,stack),flags,name);
}
static SDL_atomic_t system_flags;
static SDL_atomic_t osk_event;
static SDL_atomic_t osk_unloaded;
static SDL_atomic_t osk_finish;
static SDL_atomic_t osk_trace;
#ifdef NL_NATIVE_DISPLAY
#define NL_CALLBACK_SLOT SYSUTIL_EVENT_SLOT0
#else
#define NL_CALLBACK_SLOT SYSUTIL_EVENT_SLOT1
#endif
#ifdef NL_DIAGNOSTIC
static int diagnostic_stall,diagnostic_exit;
void runtime_diagnostic_stall_flip(int exit_during_wait) {
    diagnostic_stall=1; diagnostic_exit=exit_during_wait;
}
#endif
static void system_event(u64 status,u64 parameter,void *userdata) {
    (void)parameter; (void)userdata;
    /* Release-build event trace (FREEZE2-DIAGNOSIS.md fix item 3):
     * while an OSK dialog session is armed (runtime_osk_trace), every
     * raw status in the OSK range and the draw/menu range goes to
     * youtube.log. Both 1.10 and 1.11 froze with zero close-path
     * evidence because the release build recorded callback events
     * nowhere; this makes the true hardware sequence readable from
     * one log pull. The callback runs from sysUtilCheckCallback on
     * an app thread, the same context the NL_DIAGNOSTIC log below
     * already writes from. */
    if(SDL_AtomicGet(&osk_trace)
        && ((status>=0x0502 && status<=0x0506)
            || status==0x0121 || status==0x0122
            || status==0x0131 || status==0x0132)) {
        char evbuf[48];
        snprintf(evbuf,sizeof(evbuf),"sysutil event=0x%04llx",(unsigned long long)status);
        yt_log_sink("sysutil",evbuf);
    }
    /* On-screen keyboard dialog events (0x0502-0x0506) are recorded for
     * yt_osk to poll; they do not change the system state flags. */
    if(status>=0x0502 && status<=0x0506) SDL_AtomicSet(&osk_event,(int)status);
    /* The finish events additionally go into a sticky latch (first
     * one wins): on user confirm the firmware delivers ENTERED/DONE
     * and UNLOADED back-to-back, and UNLOADED overwrites the finish
     * event in the last-event-wins osk_event before yt_osk polls it -
     * the 1.10 search freeze. yt_osk consumes finishes from here. */
    if((status==SYSUTIL_OSK_INPUT_ENTERED || status==SYSUTIL_OSK_INPUT_CANCELED || status==SYSUTIL_OSK_DONE) && SDL_AtomicGet(&osk_finish)==0)
        SDL_AtomicSet(&osk_finish,(int)status);
    /* UNLOADED additionally goes into a sticky latch: it is the
     * terminal event yt_osk waits for, and the last-event-wins
     * osk_event above can lose it to a following event. */
    if(status==SYSUTIL_OSK_UNLOADED) SDL_AtomicSet(&osk_unloaded,1);
    SDL_AtomicSet(&system_flags,system_state_event(SDL_AtomicGet(&system_flags),(unsigned)status));
#ifdef NL_DIAGNOSTIC
    FILE *log=fopen("/dev_hdd0/game/SVIZ00001/USRDIR/system-events.log","a");
    if(log) { fprintf(log,"event=0x%04llx flags=%d\n",(unsigned long long)status,SDL_AtomicGet(&system_flags)); fclose(log); }
#endif
}
int runtime_init(void) {
    SDL_AtomicSet(&system_flags,0);
    SDL_AtomicSet(&osk_event,0);
    SDL_AtomicSet(&osk_unloaded,0);
    SDL_AtomicSet(&osk_finish,0);
    SDL_AtomicSet(&osk_trace,0);
    return sysUtilRegisterCallback(NL_CALLBACK_SLOT,system_event,NULL);
}
int runtime_exit_requested(void) { return SDL_AtomicGet(&system_flags)&NL_SYSTEM_EXIT; }
int runtime_osk_event(void) { return SDL_AtomicGet(&osk_event); }
/* Clears every OSK signal. Callers: runtime_init and yt_osk_open
 * ONLY - never during the close sequence, where clearing the latches
 * after the firmware already delivered the finish batch would erase
 * the only copies of those events (the 1.10 search freeze). */
void runtime_osk_reset(void) { SDL_AtomicSet(&osk_event,0); SDL_AtomicSet(&osk_unloaded,0); SDL_AtomicSet(&osk_finish,0); }
int runtime_osk_unloaded_take(void) {
    int v=SDL_AtomicGet(&osk_unloaded);
    if(v) SDL_AtomicSet(&osk_unloaded,0);
    return v;
}
int runtime_osk_finish_take(void) {
    int v=SDL_AtomicGet(&osk_finish);
    if(v) SDL_AtomicSet(&osk_finish,0);
    return v;
}
int runtime_osk_finish_peek(void) { return SDL_AtomicGet(&osk_finish); }
void runtime_osk_trace(int on) { SDL_AtomicSet(&osk_trace,on?1:0); }
int runtime_draw_suspended(void) { return SDL_AtomicGet(&system_flags)&(NL_SYSTEM_MENU|NL_SYSTEM_DRAW); }
void runtime_shutdown(void) { sysUtilUnregisterCallback(NL_CALLBACK_SLOT); }
#ifdef NL_DIAGNOSTIC
void runtime_diagnostic_menu_open(void) {
    system_event(SYSUTIL_DRAW_BEGIN,0,NULL);
    system_event(SYSUTIL_MENU_OPEN,0,NULL);
}
#endif
extern u32 __real_gcmGetFlipStatus(void);
u32 __wrap_gcmGetFlipStatus(void) {
    static WaitGuard guard;
    u32 status=__real_gcmGetFlipStatus();
#ifdef NL_DIAGNOSTIC
    if(diagnostic_stall) status=1;
    if(diagnostic_exit) system_event(SYSUTIL_EXIT_GAME,0,NULL);
#endif
    u64 seconds,nanoseconds;
    if(sysGetCurrentTime(&seconds,&nanoseconds)) sysProcessExit(13);
    int state=wait_guard_poll(&guard,seconds*1000+nanoseconds/1000000,status!=0,2000);
    if(state==NL_WAIT_READY) return status;
    /* SDL's PS3 backend otherwise sleeps forever here without servicing the
     * system menu. This runs on the same thread as SDL's normal event pump. */
    sysUtilCheckCallback();
    if(runtime_exit_requested()) sysProcessExit(0);
    if(runtime_draw_suspended()) { guard.active=0; return status; }
    if(state==NL_WAIT_TIMEOUT) {
        FILE *log=fopen("/dev_hdd0/game/SVIZ00001/USRDIR/display-error.log","w");
        if(log) { fputs("Display flip did not finish within 2000 ms. App exited.\n",log); fclose(log); }
        sysProcessExit(12);
    }
    return status;
}
