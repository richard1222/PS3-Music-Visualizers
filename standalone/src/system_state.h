#ifndef NEON_SYSTEM_STATE_H
#define NEON_SYSTEM_STATE_H
enum { NL_SYSTEM_EXIT=1, NL_SYSTEM_MENU=2, NL_SYSTEM_DRAW=4 };
unsigned system_state_event(unsigned flags,unsigned event);
#endif
