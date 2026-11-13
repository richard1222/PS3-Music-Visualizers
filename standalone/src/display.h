#ifndef NEON_DISPLAY_H
#define NEON_DISPLAY_H
#include <SDL.h>
Uint32 display_init_flags(void);
SDL_Surface *display_open(void);
int display_present(void);
int display_idle(void);
void display_close(void);
#endif
