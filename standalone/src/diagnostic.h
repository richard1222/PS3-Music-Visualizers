#ifndef NEON_DIAGNOSTIC_H
#define NEON_DIAGNOSTIC_H
#ifdef NL_DIAGNOSTIC
#include <sys/thread.h>
#include <sys/process.h>
#include <unistd.h>
static sys_ppu_thread_t diagnostic_watchdog_id;
static SDL_atomic_t diagnostic_done;
static SDL_atomic_t diagnostic_heartbeat;
static SDL_atomic_t diagnostic_deadline_ticks;
static unsigned diagnostic_frame_limit=4;
static void diagnostic_watchdog(void *unused) {
    (void)unused;
    int previous=0;
    for(int i=0,elapsed=0;i<200;i++,elapsed++) {
        if(SDL_AtomicGet(&diagnostic_done)) sysThreadExit(0);
        /* Menu-open events must never disable the absolute test deadline. */
        if(elapsed>=SDL_AtomicGet(&diagnostic_deadline_ticks)) sysProcessExit(14);
        int current=SDL_AtomicGet(&diagnostic_heartbeat);
        if(runtime_draw_suspended()) i=0;
        if(current!=previous) { previous=current; i=0; }
        usleep(50000);
    }
    /* Independent of the event/render loop, including during cleanup. */
    sysProcessExit(14);
}
static int diagnostic_start(void) {
    SDL_AtomicSet(&diagnostic_done,0);
    SDL_AtomicSet(&diagnostic_deadline_ticks,200);
    return sysThreadCreate(&diagnostic_watchdog_id,diagnostic_watchdog,NULL,1000,32768,THREAD_JOINABLE,"neon_watchdog");
}
static void diagnostic_finish(void) {
    u64 result;
    SDL_AtomicSet(&diagnostic_done,1);
    sysThreadJoin(diagnostic_watchdog_id,&result);
}
static FILE *diagnostic_file;
static unsigned diagnostic_frames;
static unsigned diagnostic_lines;
static void diagnostic_trace(const char *stage) {
    if(!diagnostic_file) diagnostic_file=fopen(ASSETS "diagnostic.log","w");
    if(diagnostic_file && diagnostic_lines<512 && (diagnostic_frames<8 || strstr(stage,"cleanup"))) {
        diagnostic_lines++;
        fprintf(diagnostic_file,"frame=%u stage=%s\n",diagnostic_frames,stage);
        fflush(diagnostic_file);
    }
}
#define NL_TRACE(stage) diagnostic_trace(stage)
static void diagnostic_event(const char *stage) {
    if(!diagnostic_file) diagnostic_trace("event-log-ready");
    if(diagnostic_file && diagnostic_lines<512) {
        diagnostic_lines++;
        fprintf(diagnostic_file,"frame=%u event=%s\n",diagnostic_frames,stage); fflush(diagnostic_file);
    }
}
#define NL_EVENT(stage) diagnostic_event(stage)
static void diagnostic_event_value(const char *name,long long value) {
    char line[160]; snprintf(line,sizeof(line),"%s=%lld",name,value); diagnostic_event(line);
}
#define NL_EVENT_VALUE(name,value) diagnostic_event_value(name,(long long)(value))
static void diagnostic_value(const char *name,long long value) {
    char line[160]; snprintf(line,sizeof(line),"%s=%lld",name,value); diagnostic_trace(line);
}
static void diagnostic_input(unsigned buttons,int section,int focus) {
    if(buttons && diagnostic_file) {
        fprintf(diagnostic_file,"frame=%u buttons=%u section=%d focus=%d\n",diagnostic_frames,buttons,section,focus);
        fflush(diagnostic_file);
    }
}
#define NL_INPUT(buttons,section,focus) diagnostic_input(buttons,section,focus)
#define NL_VALUE(name,value) diagnostic_value(name,(long long)(value))
#define NL_FRAME_COMPLETE() do { diagnostic_frames++; SDL_AtomicAdd(&diagnostic_heartbeat,1); if(diagnostic_frames==diagnostic_frame_limit) running=0; } while(0)
#define NL_DIAGNOSTIC_START() diagnostic_start()
#define NL_DIAGNOSTIC_FINISH() diagnostic_finish()
#else
#define NL_TRACE(stage) ((void)0)
#define NL_EVENT(stage) ((void)0)
#define NL_EVENT_VALUE(name,value) ((void)0)
#define NL_VALUE(name,value) ((void)0)
#define NL_INPUT(buttons,section,focus) ((void)0)
#define NL_FRAME_COMPLETE() ((void)0)
#define NL_DIAGNOSTIC_START() 0
#define NL_DIAGNOSTIC_FINISH() ((void)0)
#endif
#endif
