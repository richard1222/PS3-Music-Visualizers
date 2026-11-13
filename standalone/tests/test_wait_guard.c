#include "wait_guard.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    WaitGuard guard={0};
    assert(wait_guard_poll(&guard,100,0,2000)==NL_WAIT_READY);
    assert(wait_guard_poll(&guard,200,1,2000)==NL_WAIT_PENDING);
    assert(wait_guard_poll(&guard,2199,1,2000)==NL_WAIT_PENDING);
    assert(wait_guard_poll(&guard,2200,1,2000)==NL_WAIT_TIMEOUT);
    assert(wait_guard_poll(&guard,2201,0,2000)==NL_WAIT_READY);
    assert(wait_guard_poll(&guard,5000,1,2000)==NL_WAIT_PENDING);
    assert(wait_guard_poll(&guard,5010,0,2000)==NL_WAIT_READY);
    assert(wait_guard_poll(&guard,8000,1,2000)==NL_WAIT_PENDING);
    assert(wait_guard_poll(&guard,7999,1,2000)==NL_WAIT_TIMEOUT);
    puts("Display guard: ready, delayed flip, deadline, next frame and clock rollback passed");
    return 0;
}
