#include "system_state.h"
unsigned system_state_event(unsigned flags,unsigned event) {
    switch(event) {
        case 0x0101: return flags|NL_SYSTEM_EXIT;
        case 0x0121: return flags|NL_SYSTEM_DRAW;
        case 0x0122: return flags&~NL_SYSTEM_DRAW;
        case 0x0131: return flags|NL_SYSTEM_MENU;
        case 0x0132: return flags&~NL_SYSTEM_MENU;
        default: return flags;
    }
}
