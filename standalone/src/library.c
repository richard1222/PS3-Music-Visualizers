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
        if(!S_ISDIR(st.st_mode) && !(S_ISREG(st.st_mode) && is_mp3(entry->d_name))) continue;
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
