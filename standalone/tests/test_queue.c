#define _DEFAULT_SOURCE
#include "library.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    /* --- repeat-mode advance matrix (v1.20) --- */
    assert(queue_advance(0,3,NL_REPEAT_OFF)==1);
    assert(queue_advance(1,3,NL_REPEAT_OFF)==2);
    assert(queue_advance(2,3,NL_REPEAT_OFF)==-1);  /* end of playlist */
    assert(queue_advance(2,3,NL_REPEAT_ALL)==0);   /* wraps */
    assert(queue_advance(0,3,NL_REPEAT_ALL)==1);
    assert(queue_advance(0,1,NL_REPEAT_ALL)==0);   /* single track wraps to itself */
    assert(queue_advance(2,3,NL_REPEAT_ONE)==2);   /* stays */
    assert(queue_advance(0,3,NL_REPEAT_ONE)==0);
    assert(queue_advance(0,0,NL_REPEAT_ALL)==-1);  /* empty queue */
    assert(queue_advance(-1,3,NL_REPEAT_OFF)==-1); /* no current track */
    assert(queue_advance(5,3,NL_REPEAT_ALL)==-1);  /* stale index */

    /* --- .m3u queue export round-trip --- */
    char dir[]="/tmp/neon-queue-XXXXXX"; assert(mkdtemp(dir));
    char p1[NL_PATH],p2[NL_PATH],mpath[NL_PATH];
    assert(!library_join(p1,dir,"one.mp3"));
    assert(!library_join(p2,dir,"two.mp3"));
    assert(!library_join(mpath,dir,"Neon Queue.m3u"));
    FILE *f=fopen(p1,"wb"); assert(f); fclose(f);
    f=fopen(p2,"wb"); assert(f); fclose(f);

    static Library lib; memset(&lib,0,sizeof(lib));
    lib.count=2;
    snprintf(lib.tracks[0].path,sizeof(lib.tracks[0].path),"%s",p1);
    snprintf(lib.tracks[1].path,sizeof(lib.tracks[1].path),"%s",p2);
    assert(library_save_m3u(mpath,&lib)==2);

    /* the file is a plain #EXTM3U with one absolute path per line */
    f=fopen(mpath,"r"); assert(f);
    char line[NL_PATH];
    assert(fgets(line,sizeof(line),f) && !strncmp(line,"#EXTM3U",7));
    assert(fgets(line,sizeof(line),f) && strstr(line,"one.mp3"));
    assert(fgets(line,sizeof(line),f) && strstr(line,"two.mp3"));
    fclose(f);

    static Library back; memset(&back,0,sizeof(back));
    assert(library_load_m3u(mpath,&back)==2);
    assert(!strcmp(back.tracks[0].path,p1));
    assert(!strcmp(back.tracks[1].path,p2));

    /* an empty queue saves a header-only file that loads as empty */
    static Library empty; memset(&empty,0,sizeof(empty));
    assert(library_save_m3u(mpath,&empty)==0);
    memset(&back,0,sizeof(back));
    assert(library_load_m3u(mpath,&back)==0);

    /* a missing file is not loadable, a bad path not saveable */
    assert(library_load_m3u("/no/such/neon/queue.m3u",&back)<=0);
    assert(library_save_m3u("/no/such/neon/queue.m3u",&lib)==-1);
    assert(library_save_m3u("",&lib)==-1);

    assert(!unlink(p1)); assert(!unlink(p2)); assert(!unlink(mpath)); assert(!rmdir(dir));
    printf("test_queue: OK\n");
    return 0;
}
