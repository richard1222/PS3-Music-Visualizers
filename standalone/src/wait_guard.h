#ifndef NEON_WAIT_GUARD_H
#define NEON_WAIT_GUARD_H
#include <stdint.h>
typedef struct { uint64_t start; int active; } WaitGuard;
enum { NL_WAIT_READY, NL_WAIT_PENDING, NL_WAIT_TIMEOUT };
int wait_guard_poll(WaitGuard *guard,uint64_t now,int busy,uint64_t timeout);
#endif
