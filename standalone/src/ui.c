#include "ui.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>
typedef struct { SDL_Surface *bitmap; int left,top,advance; } Glyph;
static FT_Library library; static FT_Face face;
static Glyph glyphs[3][96]; static int sizes[3]={18,26,42};
int ui_font_open(const char *path) {
    if(FT_Init_FreeType(&library))return -1;
    if(FT_New_Face(library,path,0,&face)) {FT_Done_FreeType(library);library=0;return -1;}
    return 0;
}
void ui_font_close(void) {
    int i,j;for(i=0;i<3;i++)for(j=0;j<96;j++)SDL_FreeSurface(glyphs[i][j].bitmap);
    if(face)FT_Done_Face(face);
    if(library)FT_Done_FreeType(library);
    face=0;library=0;memset(glyphs,0,sizeof(glyphs));
}
void ui_rect(SDL_Surface *s,int x,int y,int w,int h,Uint32 rgb) {
    SDL_Rect r={x*s->w/1280,y*s->h/720,w*s->w/1280,h*s->h/720};
    SDL_FillRect(s,&r,SDL_MapRGB(s->format,(rgb>>16)&255,(rgb>>8)&255,rgb&255));
}
void ui_text(SDL_Surface *s,int x,int y,int pixels,Uint32 rgb,const char *str) {
    int level=pixels>=36?2:(pixels>=23?1:0),sx=x*s->w/1280,sy=y*s->h/720;
    if(!face)return;
    while(*str && sx<s->w-12) {
        unsigned char c=(unsigned char)*str++; Glyph *g;int xx,yy;
        if(c<32 || c>126)c='?';
        g=&glyphs[level][c-32];
        if(!g->bitmap) {
            FT_Set_Pixel_Sizes(face,0,sizes[level]*s->h/720);
            if(FT_Load_Char(face,c,FT_LOAD_RENDER))continue;
            FT_GlyphSlot slot=face->glyph;
            g->left=slot->bitmap_left;g->top=slot->bitmap_top;g->advance=slot->advance.x>>6;
            g->bitmap=SDL_CreateRGBSurfaceWithFormat(0,slot->bitmap.width?slot->bitmap.width:1,slot->bitmap.rows?slot->bitmap.rows:1,32,SDL_PIXELFORMAT_ARGB8888);
            if(!g->bitmap)continue;
            SDL_FillRect(g->bitmap,0,0);
            for(yy=0;yy<(int)slot->bitmap.rows;yy++)for(xx=0;xx<(int)slot->bitmap.width;xx++)
                ((Uint32*)((Uint8*)g->bitmap->pixels+yy*g->bitmap->pitch))[xx]=((Uint32)slot->bitmap.buffer[yy*slot->bitmap.pitch+xx]<<24)|0xffffff;
            SDL_SetSurfaceBlendMode(g->bitmap,SDL_BLENDMODE_BLEND);
        }
        SDL_SetSurfaceColorMod(g->bitmap,(rgb>>16)&255,(rgb>>8)&255,rgb&255);
        SDL_Rect dst={sx+g->left,sy+sizes[level]*s->h/720-g->top,0,0};
        SDL_BlitSurface(g->bitmap,0,s,&dst);sx+=g->advance;
    }
}
void ui_line(SDL_Surface *s,int x0,int y0,int x1,int y1,Uint32 rgb) {
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;
    for(;;) {ui_rect(s,x0,y0,2,2,rgb);if(x0==x1 && y0==y1)break;int e2=2*e;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}}
}
