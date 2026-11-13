#include "gallery.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
static const struct { const char *file; const char *title; } known_titles[]={
    {"fate.bmp","Rin Tohsaka / Saber"},
    {"one-piece.bmp","Nami / Nico Robin"},
    {"miku.bmp","Hatsune Miku"},
    {"dxd.bmp","Rias Gremory / Akeno Himejima"},
    {"anya.bmp","Anya Forger"},
    {"frieren.bmp","Frieren"},
    {"mitsuri.bmp","Mitsuri Kanroji"}
};
static int name_rank(const char *name) {
    for(size_t i=0;i<sizeof(known_titles)/sizeof(known_titles[0]);i++) if(!strcasecmp(name,known_titles[i].file)) return (int)i;
    return 100;
}
static int name_compare(const void *a,const void *b) {
    const char *x=(const char*)a,*y=(const char*)b;
    int rx=name_rank(x),ry=name_rank(y);
    return rx!=ry?rx-ry:strcasecmp(x,y);
}
static void title_for(const char *name,char *out,size_t size) {
    for(size_t i=0;i<sizeof(known_titles)/sizeof(known_titles[0]);i++) {
        if(!strcasecmp(name,known_titles[i].file)) { snprintf(out,size,"%s",known_titles[i].title); return; }
    }
    size_t len=strlen(name);
    const char *ext=strrchr(name,'.');
    if(ext) len=(size_t)(ext-name);
    if(len>=size) len=size-1;
    for(size_t i=0;i<len;i++) { char c=name[i]; out[i]=(c=='_'||c=='-')?' ':c; }
    out[len]=0;
}
void gallery_open(Gallery *g,const char *folder) {
    memset(g,0,sizeof(*g));g->index=-1;g->changed=SDL_GetTicks();
    if(!folder || !*folder) return;
    if(snprintf(g->folder,sizeof(g->folder),"%s",folder)>=(int)sizeof(g->folder)) { g->folder[0]=0; return; }
    DIR *dir=opendir(folder);
    if(!dir) return;
    char names[64][256]; int count=0;
    struct dirent *entry;
    while(count<64 && (entry=readdir(dir))) {
        const char *ext;
        if(entry->d_name[0]=='.') continue;
        ext=strrchr(entry->d_name,'.');
        if(!ext || strcasecmp(ext,".bmp")) continue;
        if(strlen(entry->d_name)>=256) continue;
        snprintf(names[count],sizeof(names[count]),"%s",entry->d_name); count++;
    }
    closedir(dir);
    qsort(names,count,sizeof(names[0]),name_compare);
    for(int i=0;i<count && g->loaded<NL_GALLERY_SLIDES;i++) {
        char path[768];int n=snprintf(path,sizeof(path),"%s/%s",folder,names[i]);
        if(n<0 || n>=(int)sizeof(path)) continue;
        SDL_Surface *image=SDL_LoadBMP(path);
        if(!image) continue;
        if(image->w<1 || image->h<1 || image->w>1280 || image->h>720) {SDL_FreeSurface(image);continue;}
        int slot=g->loaded;
        g->images[slot]=image;
        memcpy(g->names[slot],names[i],strlen(names[i])+1);
        title_for(names[i],g->titles[slot],sizeof(g->titles[slot]));
        g->loaded++;
    }
    if(g->loaded) g->index=0;
}
void gallery_close(Gallery *g) {
    for(int i=0;i<NL_GALLERY_SLIDES;i++) SDL_FreeSurface(g->images[i]);
    memset(g,0,sizeof(*g));g->index=-1;
}
void gallery_step(Gallery *g,int direction,Uint32 now) {
    if(g->index<0 || !g->loaded) return;
    for(int i=0;i<NL_GALLERY_SLIDES;i++) {
        g->index=(g->index+(direction<0?NL_GALLERY_SLIDES-1:1))%NL_GALLERY_SLIDES;
        if(g->images[g->index]) break;
    }
    g->changed=now;
}
void gallery_tick(Gallery *g,Uint32 now) {
    if((Uint32)(now-g->changed)>=12000) gallery_step(g,1,now);
}
const char *gallery_title(const Gallery *g) {return (g->index>=0 && g->index<NL_GALLERY_SLIDES && g->images[g->index])?g->titles[g->index]:"Gallery artwork unavailable";}
void gallery_draw(Gallery *g,SDL_Surface *screen,int x,int y,int w,int h,Uint32 now) {
    if(g->index<0) return;
    SDL_Surface *image=g->images[g->index];
    SDL_Rect area={x*screen->w/1280,y*screen->h/720,w*screen->w/1280,h*screen->h/720};
    SDL_Rect old;SDL_GetClipRect(screen,&old);SDL_SetClipRect(screen,&area);
    /* A dim copy fills the frame. The moving foreground preserves the whole
     * image at its original aspect ratio, with no cropped heads or stretching. */
    SDL_SetSurfaceColorMod(image,40,40,40);SDL_BlitScaled(image,NULL,screen,&area);
    SDL_SetSurfaceColorMod(image,255,255,255);
    float scale=fminf((float)area.w/image->w,(float)area.h/image->h);
    float phase=(Uint32)(now-g->changed)*0.0005235988f;
    scale*=0.975f+0.015f*sinf(phase);
    int iw=(int)(image->w*scale),ih=(int)(image->h*scale);
    SDL_Rect dst={area.x+(area.w-iw)/2,area.y+(area.h-ih)/2,iw,ih};
    SDL_BlitScaled(image,NULL,screen,&dst);SDL_SetClipRect(screen,&old);
}
static int gallery_fail(char *err,size_t size,const char *msg) {
    if(err && size) snprintf(err,size,"%s",msg);
    return -1;
}
static const char *gallery_basename(const char *path) {
    const char *s=strrchr(path,'/');
    return s?s+1:path;
}
int gallery_add(Gallery *g,const char *src_path,char *err,size_t err_size) {
    const char *name,*ext;
    char dest[768],folder[512];
    FILE *probe,*in,*out;
    unsigned char buf[32768];
    long total=0;
    int ok=1;
    size_t got;
    SDL_Surface *image;
    if(!g || !src_path || !*src_path || !g->folder[0]) return gallery_fail(err,err_size,"Gallery unavailable.");
    name=gallery_basename(src_path);
    if(!*name || !strcmp(name,".") || !strcmp(name,"..") || strchr(name,'\\') || strlen(name)>=256)
        return gallery_fail(err,err_size,"That file name is not supported.");
    ext=strrchr(name,'.');
    if(!ext || strcasecmp(ext,".bmp")) return gallery_fail(err,err_size,"Only BMP pictures can be added.");
    if(g->loaded>=NL_GALLERY_SLIDES) return gallery_fail(err,err_size,"Gallery is full (7). Remove one first.");
    if(snprintf(dest,sizeof(dest),"%s/%s",g->folder,name)>=(int)sizeof(dest))
        return gallery_fail(err,err_size,"That file name is too long.");
    probe=fopen(dest,"rb");
    if(probe) { fclose(probe); return gallery_fail(err,err_size,"That artwork is already in the gallery."); }
    in=fopen(src_path,"rb");
    if(!in) return gallery_fail(err,err_size,"Could not open that picture.");
    out=fopen(dest,"wb");
    if(!out) { fclose(in); return gallery_fail(err,err_size,"Could not save to the gallery folder."); }
    while((got=fread(buf,1,sizeof(buf),in))>0) {
        total+=(long)got;
        if(total>NL_GALLERY_MAX_BYTES || fwrite(buf,1,got,out)!=got) { ok=0; break; }
    }
    if(ferror(in)) ok=0;
    fclose(in); fclose(out);
    if(!ok) { remove(dest); return gallery_fail(err,err_size,"Picture is too large or could not be copied (8 MB max)."); }
    image=SDL_LoadBMP(dest);
    if(!image || image->w<1 || image->h<1 || image->w>1280 || image->h>720) {
        if(image) SDL_FreeSurface(image);
        remove(dest);
        return gallery_fail(err,err_size,"Not a supported BMP (1280x720 or smaller).");
    }
    SDL_FreeSurface(image);
    snprintf(folder,sizeof(folder),"%s",g->folder);
    gallery_close(g); gallery_open(g,folder);
    return 0;
}
int gallery_remove_current(Gallery *g,char *err,size_t err_size) {
    char folder[512],name[256],src[768],parent[512],removed_dir[768],backup[768];
    char *slash;
    if(!g || g->index<0 || g->index>=NL_GALLERY_SLIDES || !g->images[g->index] || !g->folder[0])
        return gallery_fail(err,err_size,"No artwork to remove.");
    snprintf(folder,sizeof(folder),"%s",g->folder);
    snprintf(name,sizeof(name),"%s",g->names[g->index]);
    if(snprintf(src,sizeof(src),"%s/%s",folder,name)>=(int)sizeof(src))
        return gallery_fail(err,err_size,"Could not remove that artwork.");
    snprintf(parent,sizeof(parent),"%s",folder);
    slash=strrchr(parent,'/');
    if(!slash) return gallery_fail(err,err_size,"Could not remove that artwork.");
    *slash=0;
    if(snprintf(removed_dir,sizeof(removed_dir),"%s/removed",parent)>=(int)sizeof(removed_dir))
        return gallery_fail(err,err_size,"Could not remove that artwork.");
    mkdir(removed_dir,0777);
    if(snprintf(backup,sizeof(backup),"%s/%s",removed_dir,name)>=(int)sizeof(backup))
        return gallery_fail(err,err_size,"Could not remove that artwork.");
    remove(backup);
    if(rename(src,backup)) return gallery_fail(err,err_size,"Could not remove that artwork.");
    gallery_close(g); gallery_open(g,folder);
    return 0;
}
