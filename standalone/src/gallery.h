#ifndef NL_GALLERY_H
#define NL_GALLERY_H
#include <SDL.h>
#include <stddef.h>
#define NL_GALLERY_SLIDES 7
#define NL_GALLERY_MAX_BYTES (8*1024*1024)
typedef struct {
    SDL_Surface *images[NL_GALLERY_SLIDES];
    char names[NL_GALLERY_SLIDES][256];
    char titles[NL_GALLERY_SLIDES][64];
    char folder[512];
    int index,loaded;
    Uint32 changed;
} Gallery;
void gallery_open(Gallery *gallery,const char *folder);
void gallery_close(Gallery *gallery);
void gallery_step(Gallery *gallery,int direction,Uint32 now);
void gallery_tick(Gallery *gallery,Uint32 now);
void gallery_draw(Gallery *gallery,SDL_Surface *screen,int x,int y,int w,int h,Uint32 now);
const char *gallery_title(const Gallery *gallery);
int gallery_add(Gallery *gallery,const char *src_path,char *err,size_t err_size);
int gallery_remove_current(Gallery *gallery,char *err,size_t err_size);
#endif
