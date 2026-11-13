#include "system_state.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    unsigned flags=0;
    flags=system_state_event(flags,0x0121); assert(flags==NL_SYSTEM_DRAW);
    flags=system_state_event(flags,0x0131); assert(flags==(NL_SYSTEM_DRAW|NL_SYSTEM_MENU));
    flags=system_state_event(flags,0x0122); assert(flags==NL_SYSTEM_MENU);
    flags=system_state_event(flags,0x0132); assert(!flags);
    flags=system_state_event(flags,0x0131); assert(flags==NL_SYSTEM_MENU);
    flags=system_state_event(flags,0x0101); assert(flags&NL_SYSTEM_EXIT);
    flags=system_state_event(flags,0x0132); assert(flags==NL_SYSTEM_EXIT);
    assert(system_state_event(flags,0x7777)==flags);
    flags=system_state_event(0,0x0122); assert(!flags);
    puts("System menu: draw/menu ordering, resume, sticky exit and unknown events passed");
    return 0;
}
