#ifndef SABER_UI_H
#define SABER_UI_H
#include <SDL.h>
int ui_font_open(const char *path);
void ui_font_close(void);
void ui_text(SDL_Surface *screen,int x,int y,int pixels,Uint32 rgb,const char *text);
void ui_rect(SDL_Surface *screen,int x,int y,int w,int h,Uint32 rgb);
void ui_line(SDL_Surface *screen,int x0,int y0,int x1,int y1,Uint32 rgb);
#endif
