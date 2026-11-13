#include "thread_stack.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(thread_stack_bytes("SDL",16384)==NL_SDL_STACK_BYTES);
    assert(thread_stack_bytes("SDL",1048576)==1048576);
    assert(thread_stack_bytes("neon_watchdog",32768)==32768);
    assert(thread_stack_bytes("Other",16384)==16384);
    assert(thread_stack_bytes(NULL,32768)==32768);
    /* Decoder alone is 16880 bytes at -O1 on the PS3 compiler; leave room
     * for SDL conversion, MP3 helpers, callback IO and the thread runner. */
    assert(NL_SDL_STACK_BYTES>16880+65536);
    puts("Thread stacks: SDL minimum, larger requests, non-SDL threads and decoder headroom passed");
    return 0;
}
