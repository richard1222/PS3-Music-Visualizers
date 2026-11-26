#define _DEFAULT_SOURCE
#include "library.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#ifdef NEON_PS3
/* PSL1GHT newlib exposes stat, but has no lstat implementation. Traversal
 * remains bounded by depth, path length and track count on the console. */
#define library_stat stat
#else
#define library_stat lstat
#endif
static int scan_budget;
static struct { dev_t dev; ino_t ino; } seen_dirs[1024];
static int seen_count;
static int dir_seen(dev_t dev,ino_t ino) {
    for(int i=0;i<seen_count;i++) if(seen_dirs[i].dev==dev && seen_dirs[i].ino==ino) return 1;
    return 0;
}
static void dir_remember(dev_t dev,ino_t ino) {
    if(seen_count<1024) { seen_dirs[seen_count].dev=dev; seen_dirs[seen_count].ino=ino; seen_count++; }
}
static int is_mp3(const char *s) {
    const char *ext=strrchr(s,'.');
    return ext && !strcasecmp(ext,".mp3");
}
static int is_bmp(const char *s) {
    const char *ext=strrchr(s,'.');
    return ext && !strcasecmp(ext,".bmp");
}
static int is_m3u(const char *s) {
    const char *ext=strrchr(s,'.');
    return ext && (!strcasecmp(ext,".m3u") || !strcasecmp(ext,".m3u8"));
}
int library_is_m3u_name(const char *name) { return name && is_m3u(name); }
int library_join(char *out,const char *root,const char *name) {
    if(!out || !root || !*root || !name || !*name || strchr(name,'/') || !strcmp(name,"..") || !strcmp(name,".")) return -1;
    return snprintf(out,NL_PATH,"%s%s%s",root,root[strlen(root)-1]=='/'?"":"/",name)>=NL_PATH?-1:0;
}
static void scan(Library *l,const char *root,int depth) {
    DIR *dir=opendir(root); struct dirent *entry;
    if(!dir) { l->errors++; return; }
    while((entry=readdir(dir))) {
        char path[NL_PATH]; struct stat st;
        if(--scan_budget<0 || l->count==NL_TRACKS) { l->truncated=1; break; }
        if(entry->d_name[0]=='.' || strlen(entry->d_name)>=256) continue;
        if(library_join(path,root,entry->d_name) || library_stat(path,&st)) { l->errors++; continue; }
        if(S_ISDIR(st.st_mode)) {
            /* On PS3, stat follows symlinks, so a symlinked directory loop
             * would revisit the same directory. Skip directories already
             * seen by (device, inode). An inode of 0 means the filesystem
             * reports none, so tracking is skipped rather than risking
             * false matches; depth and budget bounds still apply. */
            if(st.st_ino!=0 && dir_seen(st.st_dev,st.st_ino)) continue;
            if(st.st_ino!=0) dir_remember(st.st_dev,st.st_ino);
            if(depth<8) scan(l,path,depth+1); else l->truncated=1;
        } else if(S_ISREG(st.st_mode) && is_mp3(entry->d_name)) {
            if(l->count==NL_TRACKS) { l->truncated=1; break; }
            Track *t=&l->tracks[l->count++];
            snprintf(t->path,sizeof(t->path),"%s",path);
            memcpy(t->name,entry->d_name,strlen(entry->d_name)+1);
            t->mtime=(long)st.st_mtime;
        }
    }
    closedir(dir);
}
static int track_compare(const void *a,const void *b) { return strcasecmp(((const Track*)a)->path,((const Track*)b)->path); }
int library_scan(Library *l,const char *root) {
    memset(l,0,sizeof(*l));
    if(!root || !*root) return -1;
    seen_count=0;
    {
        struct stat st;
        if(library_stat(root,&st)==0 && st.st_ino!=0) dir_remember(st.st_dev,st.st_ino);
    }
    scan_budget=8192;
    scan(l,root,0); qsort(l->tracks,l->count,sizeof(Track),track_compare);
    return l->count;
}
static int entry_compare(const void *a,const void *b) {
    const Entry *x=a,*y=b;
    return x->directory!=y->directory?y->directory-x->directory:strcasecmp(x->name,y->name);
}
int library_browse(const char *root,Entry *entries,int capacity) {
    DIR *dir=opendir(root); struct dirent *entry; int n=0;
    if(!dir) return -1;
    while(n<capacity && (entry=readdir(dir))) {
        char path[NL_PATH]; struct stat st;
        if(entry->d_name[0]=='.' || strlen(entry->d_name)>=256 || library_join(path,root,entry->d_name) || library_stat(path,&st)) continue;
        if(!S_ISDIR(st.st_mode) && !(S_ISREG(st.st_mode) && (is_mp3(entry->d_name) || is_m3u(entry->d_name)))) continue;
        memcpy(entries[n].name,entry->d_name,strlen(entry->d_name)+1);
        entries[n++].directory=S_ISDIR(st.st_mode);
    }
    closedir(dir); qsort(entries,n,sizeof(Entry),entry_compare); return n;
}
int library_browse_bmp(const char *root,Entry *entries,int capacity) {
    DIR *dir=opendir(root); struct dirent *entry; int n=0;
    if(!dir) return -1;
    while(n<capacity && (entry=readdir(dir))) {
        char path[NL_PATH]; struct stat st;
        if(entry->d_name[0]=='.' || strlen(entry->d_name)>=256 || library_join(path,root,entry->d_name) || library_stat(path,&st)) continue;
        if(!S_ISDIR(st.st_mode) && !(S_ISREG(st.st_mode) && is_bmp(entry->d_name))) continue;
        memcpy(entries[n].name,entry->d_name,strlen(entry->d_name)+1);
        entries[n++].directory=S_ISDIR(st.st_mode);
    }
    closedir(dir); qsort(entries,n,sizeof(Entry),entry_compare); return n;
}

/* --- .m3u playlist import (v1.19) -------------------------------------
 * Parses a plain .m3u/.m3u8 file into a Library the player can use as
 * its queue. Lines that are blank or start with '#' (the #EXTM3U
 * header, #EXTINF metadata) are skipped. Entries may be absolute or
 * relative to the playlist's own folder; Windows backslashes and a
 * leading file:// are tolerated, as is a UTF-8 BOM and CRLF endings.
 * Only entries that exist as regular .mp3 files are kept, so a
 * playlist written on a PC for a different drive layout degrades to
 * the tracks that do resolve instead of failing outright. */
int library_load_m3u(const char *path,Library *out) {
    memset(out,0,sizeof(*out));
    if(!path || !*path) return -1;
    FILE *f=fopen(path,"r");
    if(!f) return -1;
    char dir[NL_PATH];
    snprintf(dir,sizeof(dir),"%s",path);
    char *slash=strrchr(dir,'/');
    if(slash) *slash=0; else dir[0]=0;
    char line[NL_PATH];
    int first=1;
    while(fgets(line,sizeof(line),f)) {
        char *e=line;
        if(first) {
            first=0;
            if((unsigned char)e[0]==0xEF && (unsigned char)e[1]==0xBB && (unsigned char)e[2]==0xBF) e+=3;
        }
        while(*e==' ' || *e=='\t') e++;
        size_t len=strlen(e);
        while(len>0 && (e[len-1]=='\n' || e[len-1]=='\r' || e[len-1]==' ' || e[len-1]=='\t')) e[--len]=0;
        if(!len || e[0]=='#') continue;
        if(!strncasecmp(e,"file://",7)) e+=7;
        for(char *p=e;*p;p++) if(*p=='\\') *p='/';
        char full[NL_PATH];
        if(e[0]=='/') {
            if(snprintf(full,sizeof(full),"%s",e)>=NL_PATH) continue;
        } else {
            if(!dir[0]) continue;
            if(snprintf(full,sizeof(full),"%s/%s",dir,e)>=NL_PATH) continue;
        }
        if(!is_mp3(full)) continue;
        struct stat st;
        if(library_stat(full,&st) || !S_ISREG(st.st_mode)) continue;
        /* Keep reading past a full queue so truncation is reported,
         * like library_scan does, instead of looking complete. */
        if(out->count>=NL_TRACKS) { out->truncated=1; continue; }
        Track *t=&out->tracks[out->count++];
        snprintf(t->path,sizeof(t->path),"%s",full);
        const char *base=strrchr(full,'/');
        snprintf(t->name,sizeof(t->name),"%.255s",base?base+1:full);
        t->mtime=(long)st.st_mtime;
    }
    fclose(f);
    return out->count;
}

/* --- .m3u playlist export (v1.20) -------------------------------------
 * The mirror of the import above: the current queue written as one
 * absolute path per line. Absolute paths keep the file meaningful
 * wherever it is opened from; the importer resolves them as-is. */
int library_save_m3u(const char *path,const Library *library) {
    if(!path || !*path || !library) return -1;
    FILE *f=fopen(path,"w");
    if(!f) return -1;
    fprintf(f,"#EXTM3U\n");
    for(int i=0;i<library->count;i++) fprintf(f,"%s\n",library->tracks[i].path);
    fclose(f);
    return library->count;
}

/* --- Repeat-mode advance (v1.20) ---------------------------------------
 * One place decides what a natural track end does, so the rule the
 * UI applies is the rule the host tests assert. */
int queue_advance(int cur,int count,int repeat_mode) {
    if(count<=0 || cur<0 || cur>=count) return -1;
    if(repeat_mode==NL_REPEAT_ONE) return cur;
    if(cur+1<count) return cur+1;
    return repeat_mode==NL_REPEAT_ALL?0:-1;
}
