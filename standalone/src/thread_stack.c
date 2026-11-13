#include "thread_stack.h"
#include <string.h>
uint64_t thread_stack_bytes(const char *name,uint64_t requested) {
    if(name && !strcmp(name,"SDL") && requested<NL_SDL_STACK_BYTES) return NL_SDL_STACK_BYTES;
    return requested;
}
