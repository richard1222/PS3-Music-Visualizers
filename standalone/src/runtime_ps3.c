#include "runtime_ps3.h"
#include "wait_guard.h"
#include "system_state.h"
#include "thread_stack.h"
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
    SDL_AtomicSet(&system_flags,system_state_event(SDL_AtomicGet(&system_flags),(unsigned)status));
#ifdef NL_DIAGNOSTIC
    FILE *log=fopen("/dev_hdd0/game/SVIZ00001/USRDIR/system-events.log","a");
    if(log) { fprintf(log,"event=0x%04llx flags=%d\n",(unsigned long long)status,SDL_AtomicGet(&system_flags)); fclose(log); }
#endif
}
int runtime_init(void) {
    SDL_AtomicSet(&system_flags,0);
    return sysUtilRegisterCallback(NL_CALLBACK_SLOT,system_event,NULL);
}
int runtime_exit_requested(void) { return SDL_AtomicGet(&system_flags)&NL_SYSTEM_EXIT; }
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
