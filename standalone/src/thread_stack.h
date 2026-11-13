#ifndef NEON_THREAD_STACK_H
#define NEON_THREAD_STACK_H
#include <stdint.h>
#define NL_SDL_STACK_BYTES (256u*1024u)
uint64_t thread_stack_bytes(const char *name,uint64_t requested);
#endif
