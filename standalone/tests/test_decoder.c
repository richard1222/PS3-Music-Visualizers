#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#define MINIMP3_NO_STDIO
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
static size_t read_cb(void *buffer,size_t size,void *user) { return fread(buffer,1,size,(FILE*)user); }
static int seek_cb(uint64_t offset,void *user) { return offset>LONG_MAX?-1:fseek((FILE*)user,(long)offset,SEEK_SET); }
int main(int argc,char **argv) {
    static mp3dec_ex_t decoder; static mp3d_sample_t buffer[4096];
    assert(argc==2); FILE *file=fopen(argv[1],"rb"); assert(file);
    mp3dec_io_t io={read_cb,file,seek_cb,file};
    assert(!mp3dec_ex_open_cb(&decoder,&io,MP3D_DO_NOT_SCAN));
    assert(decoder.info.hz>=8000 && decoder.info.hz<=48000);
    assert(decoder.info.channels==1 || decoder.info.channels==2);
    size_t count,total=0,nonzero=0;
    while((count=mp3dec_ex_read(&decoder,buffer,4096))) {
        for(size_t i=0;i<count;i++) if(buffer[i]) nonzero++;
        total+=count; assert(total<48000ULL*2*60*60);
    }
    assert(total>0 && nonzero>0); assert(!decoder.last_error);
    printf("Decoder: complete MP3 stream, %d Hz, %d channels, %zu samples, clean EOF\n",decoder.info.hz,decoder.info.channels,total);
    mp3dec_ex_close(&decoder); fclose(file);
    file=tmpfile(); assert(file); fputs("invalid MP3 file",file); rewind(file);
    io.read_data=file; io.seek_data=file;
    int error=mp3dec_ex_open_cb(&decoder,&io,MP3D_DO_NOT_SCAN);
    assert(error || decoder.info.hz==0);
    mp3dec_ex_close(&decoder); fclose(file);
    puts("Decoder: invalid-file cleanup passed"); return 0;
}
