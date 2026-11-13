#include "wait_guard.h"
int wait_guard_poll(WaitGuard *guard,uint64_t now,int busy,uint64_t timeout) {
    if(!busy) { guard->active=0; return NL_WAIT_READY; }
    if(!guard->active) { guard->active=1; guard->start=now; }
    return now-guard->start>=timeout?NL_WAIT_TIMEOUT:NL_WAIT_PENDING;
}
