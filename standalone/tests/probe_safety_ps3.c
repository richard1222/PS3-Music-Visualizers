/* Offline emulator probe only. This executable intentionally stalls. */
#include <SDL.h>
#include <sys/process.h>
#include <unistd.h>
#include "safety.h"
SYS_PROCESS_PARAM(1001,0x100000);
int main(void) {
    if(SDL_Init(SDL_INIT_TIMER) || safety_start()) return 2;
    /* A responsive app must survive beyond the ten-second stall deadline. */
    for(int i=0;i<120;i++) { safety_beat(); usleep(100000); }
    /* Now stop heartbeats. The independent watchdog must exit with 15. */
    for(int i=0;i<150;i++) usleep(100000);
    return 3;
}
