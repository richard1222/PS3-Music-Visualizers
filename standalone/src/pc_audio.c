#include "pc_audio.h"
#include "pcm_ring.h"
#include <SDL.h>
#include <sys/time.h>
#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
static PcmRing ring;
static SDL_mutex *lock;
static SDL_Thread *worker;
static SDL_atomic_t cancel,state;
static int progress(void *p,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d) {
    (void)p;(void)a;(void)b;(void)c;(void)d;
    return SDL_AtomicGet(&cancel);
}
static size_t receive(void *data,size_t size,size_t n,void *p) {
    (void)p;
    if(SDL_AtomicGet(&cancel)) return 0;
    SDL_LockMutex(lock);
    pcm_ring_write(&ring,data,size*n);
    if(ring.count>=28800) SDL_AtomicSet(&state,3);
    SDL_UnlockMutex(lock);
    return size*n;
}
static int run(void *p) {
    (void)p;
    char response[128],url[160],token[33],extra;
    unsigned port=0;
    struct sockaddr_in dest,from;
    int fd=socket(AF_INET,SOCK_DGRAM,0),yes=1,found=0;
    if(fd<0) goto done;
    setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes));
    struct timeval timeout={0,200000};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    memset(&dest,0,sizeof(dest)); dest.sin_family=AF_INET;
    dest.sin_port=htons(45832); dest.sin_addr.s_addr=INADDR_BROADCAST;
    for(int i=0;i<25 && !SDL_AtomicGet(&cancel);i++) {
        if(i%5==0) sendto(fd,"NLPC1",5,0,(struct sockaddr*)&dest,sizeof(dest));
        socklen_t len=sizeof(from);
        int n=recvfrom(fd,response,sizeof(response)-1,0,(struct sockaddr*)&from,&len);
        if(n<=0) continue;
        response[n]=0;
        if(sscanf(response,"NLPC1 %u %32[0123456789abcdef] %c",&port,token,&extra)!=2 ||
           strlen(token)!=32 || !port || port>65535) continue;
        unsigned ip=ntohl(from.sin_addr.s_addr);
        if(!((ip>>24)==10 || (ip>>20)==0xac1 || (ip>>16)==0xc0a8)) continue;
        snprintf(url,sizeof(url),"http://%s:%u/pcm/%s",inet_ntoa(from.sin_addr),port,token);
        found=1; break;
    }
    close(fd);
    if(found && !SDL_AtomicGet(&cancel)) {
        CURL *c=curl_easy_init();
        if(c) {
            SDL_AtomicSet(&state,2);
            curl_easy_setopt(c,CURLOPT_URL,url);
            curl_easy_setopt(c,CURLOPT_PROXY,"");
            curl_easy_setopt(c,CURLOPT_PROTOCOLS,(long)CURLPROTO_HTTP);
            curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);
            curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
            curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,3L);
            curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
            curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1L);
            curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,5L);
            curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);
            curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);
            curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
            curl_easy_perform(c);
            curl_easy_cleanup(c);
        }
    }
done:
    SDL_AtomicSet(&state,-1); return 0;
}
int pc_audio_start(void) {
    pc_audio_stop(); memset(&ring,0,sizeof(ring));
    lock=SDL_CreateMutex(); if(!lock) return -1;
    SDL_AtomicSet(&cancel,0); SDL_AtomicSet(&state,1);
    worker=SDL_CreateThread(run,"PC audio",NULL);
    if(!worker) { pc_audio_stop(); return -1; }
    return 0;
}
void pc_audio_stop(void) {
    SDL_AtomicSet(&cancel,1);
    if(worker) SDL_WaitThread(worker,NULL);
    worker=NULL;
    if(lock) SDL_DestroyMutex(lock);
    lock=NULL; SDL_AtomicSet(&state,0);
}
size_t pc_audio_read(int16_t *samples,size_t count) {
    if(!lock || SDL_TryLockMutex(lock)!=0) { memset(samples,0,count*2); return 0; }
    size_t n=pcm_ring_read(&ring,samples,count);
    SDL_UnlockMutex(lock); return n;
}
int pc_audio_status(void) { return SDL_AtomicGet(&state); }
