#include "radio.h"
#include <SDL.h>
#include <curl/curl.h>
#include <string.h>

/* 512 KB of MP3: roughly 30 seconds at 128 kbps. When the ring is
 * full the download drops new data, which keeps a live stream live
 * instead of letting it drift minutes behind. */
#define RADIO_RING_BYTES (512*1024)
#define RADIO_START_BYTES (48*1024)
static uint8_t ring[RADIO_RING_BYTES];
static size_t ring_head,ring_count;
static SDL_mutex *lock;
static SDL_Thread *worker;
static SDL_atomic_t cancel,state;
static char stream_url[512];
static char ca_file[768];

static int progress(void *p,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d) {
    (void)p;(void)a;(void)b;(void)c;(void)d;
    return SDL_AtomicGet(&cancel);
}
static size_t receive(void *data,size_t size,size_t n,void *p) {
    (void)p;
    size_t bytes=size*n;
    if(SDL_AtomicGet(&cancel)) return 0;
    SDL_LockMutex(lock);
    for(size_t i=0;i<bytes;i++) {
        if(ring_count>=RADIO_RING_BYTES) break; /* full: drop, stay live */
        ring[(ring_head+ring_count)%RADIO_RING_BYTES]=(uint8_t)((const uint8_t*)data)[i];
        ring_count++;
    }
    if(ring_count>0 && SDL_AtomicGet(&state)<2) SDL_AtomicSet(&state,2);
    if(ring_count>=RADIO_START_BYTES) SDL_AtomicSet(&state,3);
    SDL_UnlockMutex(lock);
    return bytes;
}
static int run(void *p) {
    (void)p;
    CURL *c=curl_easy_init();
    if(c) {
        curl_easy_setopt(c,CURLOPT_URL,stream_url);
        curl_easy_setopt(c,CURLOPT_PROXY,"");
        curl_easy_setopt(c,CURLOPT_PROTOCOLS,(long)(CURLPROTO_HTTP|CURLPROTO_HTTPS));
        curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
        curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,8L);
        curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(c,CURLOPT_USERAGENT,"NeonLounge/1.19");
        curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);
        curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);
        curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
        if(ca_file[0]) {
            curl_easy_setopt(c,CURLOPT_CAINFO,ca_file);
            curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
            curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
        }
        curl_easy_perform(c);
        curl_easy_cleanup(c);
    }
    SDL_AtomicSet(&state,-1);
    return 0;
}
int radio_start(const char *url,const char *ca_path) {
    radio_stop();
    if(!url || !*url) return -1;
    if(snprintf(stream_url,sizeof(stream_url),"%s",url)>=(int)sizeof(stream_url)) return -1;
    if(ca_path) snprintf(ca_file,sizeof(ca_file),"%s",ca_path); else ca_file[0]=0;
    ring_head=0; ring_count=0;
    lock=SDL_CreateMutex();
    if(!lock) return -1;
    SDL_AtomicSet(&cancel,0);
    SDL_AtomicSet(&state,1);
    worker=SDL_CreateThread(run,"Radio stream",NULL);
    if(!worker) { radio_stop(); return -1; }
    return 0;
}
void radio_stop(void) {
    SDL_AtomicSet(&cancel,1);
    if(worker) SDL_WaitThread(worker,NULL);
    worker=NULL;
    if(lock) SDL_DestroyMutex(lock);
    lock=NULL;
    SDL_AtomicSet(&state,0);
}
int radio_status(void) { return SDL_AtomicGet(&state); }
size_t radio_buffered(void) {
    size_t n=0;
    if(!lock || SDL_TryLockMutex(lock)!=0) return 0;
    n=ring_count;
    SDL_UnlockMutex(lock);
    return n;
}
/* Decoder feed. A short wait on underrun keeps the decoder from
 * latching EOF on a network hiccup; it only ever waits when the
 * ring is empty, and gives up after ~300 ms so a dead stream ends
 * through the normal status path instead of hanging the audio. */
size_t radio_read(uint8_t *dst,size_t want) {
    if(!dst || !want) return 0;
    for(int tries=0;tries<30;tries++) {
        size_t copied=0;
        if(!lock) return 0;
        SDL_LockMutex(lock);
        while(copied<want && ring_count>0) {
            dst[copied++]=ring[ring_head];
            ring_head=(ring_head+1)%RADIO_RING_BYTES;
            ring_count--;
        }
        SDL_UnlockMutex(lock);
        if(copied) return copied;
        if(SDL_AtomicGet(&state)<0 || SDL_AtomicGet(&cancel)) return 0;
        SDL_Delay(10);
    }
    return 0;
}
