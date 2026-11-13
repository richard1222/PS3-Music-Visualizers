#define _DEFAULT_SOURCE
#include "library.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static Library library;
static void touch(const char *root,const char *name) {
    char path[NL_PATH]; assert(!library_join(path,root,name));
    FILE *f=fopen(path,"wb"); assert(f); fclose(f);
}
int main(void) {
    char root[]="/tmp/neon-library-XXXXXX",path[NL_PATH],nested[NL_PATH];
    Entry entries[10]; assert(mkdtemp(root));
    assert(!library_join(nested,root,"sub")); assert(!mkdir(nested,0700));
    touch(root,"b.MP3"); touch(root,"a.mp3"); touch(root,"ignore.wav"); touch(nested,"c.mp3");
    assert(!library_join(path,root,"cycle")); assert(!symlink(root,path));
    assert(library_scan(&library,root)==3); assert(!library.errors);
    assert(strstr(library.tracks[0].path,"a.mp3"));
    assert(library_browse(root,entries,10)==3);
    assert(entries[0].directory && !strcmp(entries[0].name,"sub"));
    assert(!library_join(path,root,"x"));
    assert(library_join(path,root,"../x")<0);
    assert(library_join(path,"","x")<0);
    assert(library_join(path,root,"..")<0);
    assert(library_browse("/no/such/neon/folder",entries,10)<0);
    assert(library_scan(&library,"/no/such/neon/folder")==0 && library.errors==1);
    for(int i=0;i<NL_TRACKS+3;i++) {
        char name[40]; snprintf(name,sizeof(name),"limit-%04d.mp3",i); touch(nested,name);
    }
    assert(library_scan(&library,root)==NL_TRACKS && library.truncated);
    for(int i=0;i<NL_TRACKS+3;i++) {
        char name[40]; snprintf(name,sizeof(name),"limit-%04d.mp3",i);
        assert(!library_join(path,nested,name)); assert(!unlink(path));
    }
    const char *files[]={"a.mp3","b.MP3","ignore.wav","cycle"};
    for(int i=0;i<4;i++) { assert(!library_join(path,root,files[i])); assert(!unlink(path)); }
    assert(!library_join(path,nested,"c.mp3")); assert(!unlink(path)); assert(!rmdir(nested)); assert(!rmdir(root));
    puts("Library: recursion, sorting, format filter, symlink cycle, invalid paths, unavailable sources and scan capacity passed");
    return 0;
}
