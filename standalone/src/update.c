#include "update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#ifdef NEON_PS3
#include <curl/curl.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sysutil/sysutil.h>
#include <unistd.h>
#endif

/* SHA-256 (FIPS 180-4), kept dependency-free so the hash check is
 * host-testable and does not depend on the TLS library. */
typedef struct { uint32_t state[8]; uint64_t bitlen; uint8_t data[64]; uint32_t datalen; } SHA256_CTX;
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))
static const uint32_t sha_k[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static void sha_transform(SHA256_CTX *ctx,const uint8_t data[]) {
    uint32_t a,b,c,d,e,f,g,h,t1,t2,m[64];
    for(int i=0,j=0;i<16;i++,j+=4) m[i]=((uint32_t)data[j]<<24)|((uint32_t)data[j+1]<<16)|((uint32_t)data[j+2]<<8)|(uint32_t)data[j+3];
    for(int i=16;i<64;i++) m[i]=SIG1(m[i-2])+m[i-7]+SIG0(m[i-15])+m[i-16];
    a=ctx->state[0];b=ctx->state[1];c=ctx->state[2];d=ctx->state[3];e=ctx->state[4];f=ctx->state[5];g=ctx->state[6];h=ctx->state[7];
    for(int i=0;i<64;i++) {
        t1=h+EP1(e)+CH(e,f,g)+sha_k[i]+m[i];t2=EP0(a)+MAJ(a,b,c);
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}
static void sha_init(SHA256_CTX *ctx) {
    ctx->datalen=0;ctx->bitlen=0;
    ctx->state[0]=0x6a09e667;ctx->state[1]=0xbb67ae85;ctx->state[2]=0x3c6ef372;ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f;ctx->state[5]=0x9b05688c;ctx->state[6]=0x1f83d9ab;ctx->state[7]=0x5be0cd19;
}
static void sha_update(SHA256_CTX *ctx,const uint8_t data[],size_t len) {
    for(size_t i=0;i<len;i++) {
        ctx->data[ctx->datalen]=data[i];ctx->datalen++;
        if(ctx->datalen==64) { sha_transform(ctx,ctx->data); ctx->bitlen+=512; ctx->datalen=0; }
    }
}
static void sha_final(SHA256_CTX *ctx,uint8_t hash[]) {
    uint32_t i=ctx->datalen;
    if(ctx->datalen<56) {
        ctx->data[i++]=0x80;
        while(i<56) ctx->data[i++]=0x00;
    } else {
        ctx->data[i++]=0x80;
        while(i<64) ctx->data[i++]=0x00;
        sha_transform(ctx,ctx->data);
        memset(ctx->data,0,56);
    }
    ctx->bitlen+=ctx->datalen*8;
    ctx->data[63]=(uint8_t)ctx->bitlen;ctx->data[62]=(uint8_t)(ctx->bitlen>>8);ctx->data[61]=(uint8_t)(ctx->bitlen>>16);ctx->data[60]=(uint8_t)(ctx->bitlen>>24);
    ctx->data[59]=(uint8_t)(ctx->bitlen>>32);ctx->data[58]=(uint8_t)(ctx->bitlen>>40);ctx->data[57]=(uint8_t)(ctx->bitlen>>48);ctx->data[56]=(uint8_t)(ctx->bitlen>>56);
    sha_transform(ctx,ctx->data);
    for(i=0;i<4;i++) for(uint32_t j=0;j<8;j++) { hash[i+(j*4)]=(uint8_t)((ctx->state[j]>>(24-i*8))&0x000000ff); }
}
int update_sha256_file(const char *path,char *hex_out) {
    FILE *f;
    SHA256_CTX ctx;
    uint8_t buf[8192],hash[32];
    size_t got;
    static const char digits[]="0123456789abcdef";
    if(!path || !hex_out) return -1;
    f=fopen(path,"rb");
    if(!f) return -1;
    sha_init(&ctx);
    while((got=fread(buf,1,sizeof(buf),f))>0) sha_update(&ctx,buf,got);
    if(ferror(f)) { fclose(f); return -1; }
    fclose(f);
    sha_final(&ctx,hash);
    for(int i=0;i<32;i++) { hex_out[i*2]=digits[hash[i]>>4]; hex_out[i*2+1]=digits[hash[i]&15]; }
    hex_out[64]=0;
    return 0;
}

int update_parse_version(const char *s,int *major,int *minor) {
    long ma=0,mi=0;
    if(!s || !*s || !major || !minor) return -1;
    while(*s>='0' && *s<='9') { ma=ma*10+(*s-'0'); if(ma>999) return -1; s++; }
    if(*s!='.') return -1;
    s++;
    if(!(*s>='0' && *s<='9')) return -1;
    while(*s>='0' && *s<='9') { mi=mi*10+(*s-'0'); if(mi>999) return -1; s++; }
    if(*s) return -1;
    *major=(int)ma;*minor=(int)mi;
    return 0;
}
int update_is_newer(const char *manifest_version) {
    int mma,mmi,cma,cmi;
    if(update_parse_version(manifest_version,&mma,&mmi)) return 0;
    if(update_parse_version(NL_VERSION,&cma,&cmi)) return 0;
    return mma>cma || (mma==cma && mmi>cmi);
}
static int json_string(const char *text,const char *key,char *out,size_t size) {
    char pattern[40];
    const char *p;
    size_t n=0;
    if(snprintf(pattern,sizeof(pattern),"\"%s\"",key)>=(int)sizeof(pattern)) return -1;
    p=strstr(text,pattern);
    if(!p) return -1;
    p+=strlen(pattern);
    while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    if(*p!=':') return -1;
    p++;
    while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    if(*p!='"') return -1;
    p++;
    while(*p && *p!='"') {
        if((unsigned char)*p<0x20 || *p=='\\') return -1;
        if(n+1>=size) return -1;
        out[n++]=*p++;
    }
    if(*p!='"' || n==0) return -1;
    out[n]=0;
    return 0;
}
int update_parse_manifest(const char *text,UpdateManifest *out) {
    int major,minor;
    if(!text || !out) return -1;
    memset(out,0,sizeof(*out));
    if(json_string(text,"version",out->version,sizeof(out->version))) return -1;
    if(json_string(text,"eboot_url",out->url,sizeof(out->url))) return -1;
    if(json_string(text,"eboot_sha256",out->sha256,sizeof(out->sha256))) return -1;
    if(update_parse_version(out->version,&major,&minor)) return -1;
    if(strncmp(out->url,"https://",8)) return -1;
    if(strlen(out->sha256)!=64) return -1;
    for(int i=0;i<64;i++) if(!isxdigit((unsigned char)out->sha256[i])) return -1;
    return 0;
}

#ifdef NEON_PS3
static int net_ready;
int update_net_init(void) {
    if(net_ready) return 0;
    if(netInitialize()!=0) return -1;
    if(netCtlInit()!=0) { netFinalizeNetwork(); return -1; }
    /* Best effort: if DHCP is still in flight on a just-booted console,
     * wait a few seconds for an IP address instead of reporting success
     * before the network can actually reach anything. A disconnected
     * interface returns immediately; the fetch still fails gracefully
     * if no address arrives. */
    for(int i=0;i<50;i++) {
        s32 state=0;
        if(netCtlGetState(&state)!=0) break;
        if(state==NET_CTL_STATE_IPObtained || state==NET_CTL_STATE_Disconnected) break;
        usleep(100000);
    }
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) { netCtlTerm(); netFinalizeNetwork(); return -1; }
    net_ready=1;
    return 0;
}
void update_net_shutdown(void) {
    if(!net_ready) return;
    curl_global_cleanup();
    netCtlTerm();
    netFinalizeNetwork();
    net_ready=0;
}
typedef struct { char *buf; size_t len,cap; } MemBuf;
static size_t mem_write(void *ptr,size_t size,size_t nmemb,void *userdata) {
    MemBuf *m=userdata;
    size_t n=size*nmemb;
    if(m->len+n>=m->cap) return 0;
    memcpy(m->buf+m->len,ptr,n);m->len+=n;m->buf[m->len]=0;
    return n;
}
typedef struct { FILE *f; long total; } FileBuf;
static size_t file_write(void *ptr,size_t size,size_t nmemb,void *userdata) {
    FileBuf *fb=userdata;
    size_t n=size*nmemb;
    fb->total+=(long)n;
    if(fb->total>NL_UPDATE_MAX_EBOOT) return 0;
    return fwrite(ptr,1,n,fb->f);
}
static int progress_cb(void *clientp,double dltotal,double dlnow,double ultotal,double ulnow) {
    (void)clientp;(void)dltotal;(void)dlnow;(void)ultotal;(void)ulnow;
    /* Keep system-menu/PS-button events dispatched during downloads. */
    sysUtilCheckCallback();
    return 0;
}
static CURL *easy_new(const char *url,long timeout) {
    CURL *c=curl_easy_init();
    char agent[32];
    if(!c) return NULL;
    snprintf(agent,sizeof(agent),"NeonLounge/%s",NL_VERSION);
    curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_USERAGENT,agent);
    curl_easy_setopt(c,CURLOPT_TIMEOUT,timeout);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);
    /* Abort a stalled transfer, but let a slow (not stuck) download finish. */
    curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1024L);
    curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,15L);
    curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(c,CURLOPT_PROGRESSFUNCTION,progress_cb);
    /* No CA bundle ships with the app, so TLS peer verification is off.
     * The downloaded EBOOT is still SHA-256 checked against the manifest,
     * which guards against corruption and bad downloads, but this does
     * not stop a determined network attacker. Full certificate checking
     * is a later hardening step, not part of this first version. */
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,0L);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,0L);
    return c;
}
static int fetch_ok(CURL *c) {
    long code=0;
    CURLcode rc=curl_easy_perform(c);
    if(rc!=CURLE_OK) return -1;
    curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
    return code==200?0:-1;
}
static int copy_file(const char *src,const char *dst) {
    FILE *in=fopen(src,"rb"),*out;
    uint8_t buf[32768];
    size_t got;
    long total=0;
    if(!in) return -1;
    out=fopen(dst,"wb");
    if(!out) { fclose(in); return -1; }
    while((got=fread(buf,1,sizeof(buf),in))>0) {
        total+=(long)got;
        if(total>NL_UPDATE_MAX_EBOOT || fwrite(buf,1,got,out)!=got) { fclose(in); fclose(out); remove(dst); return -1; }
    }
    if(ferror(in)) { fclose(in); fclose(out); remove(dst); return -1; }
    fclose(in); fclose(out);
    return 0;
}
int update_check_and_install(const char *assets_dir,char *msg,size_t msg_size) {
    char manifest_text[4096];
    MemBuf mem;
    UpdateManifest man;
    char staging[768],current[768],backup[768],tmpbak[768],hex[65];
    CURL *c;
    FILE *f;
    FileBuf fb;
    if(!msg || !msg_size) return -1;
    if(!assets_dir) { snprintf(msg,msg_size,"Update paths are unavailable."); return -1; }
    /* Retry network setup on every check: a single failure at startup
     * must not disable updates for the whole session. */
    if(!net_ready && update_net_init()) { snprintf(msg,msg_size,"Network unavailable. Check the PS3 network connection."); return -1; }
    memset(manifest_text,0,sizeof(manifest_text));
    mem.buf=manifest_text;mem.len=0;mem.cap=sizeof(manifest_text);
    c=easy_new(NL_UPDATE_URL,20L);
    if(!c) { snprintf(msg,msg_size,"Could not reach the update server."); return -1; }
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,mem_write);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&mem);
    if(fetch_ok(c)) { curl_easy_cleanup(c); snprintf(msg,msg_size,"Could not reach the update server."); return -1; }
    curl_easy_cleanup(c);
    if(update_parse_manifest(manifest_text,&man)) { snprintf(msg,msg_size,"Update info is unreadable."); return -1; }
    if(!update_is_newer(man.version)) { snprintf(msg,msg_size,"You are up to date (version %s).",NL_VERSION); return 0; }
    if(snprintf(staging,sizeof(staging),"%sEBOOT.BIN.new",assets_dir)>=(int)sizeof(staging) ||
       snprintf(current,sizeof(current),"%sEBOOT.BIN",assets_dir)>=(int)sizeof(current) ||
       snprintf(backup,sizeof(backup),"%sEBOOT.BIN.bak",assets_dir)>=(int)sizeof(backup) ||
       snprintf(tmpbak,sizeof(tmpbak),"%sEBOOT.BIN.bak.new",assets_dir)>=(int)sizeof(tmpbak)) {
        snprintf(msg,msg_size,"Update paths are too long."); return -1;
    }
    f=fopen(staging,"wb");
    if(!f) { snprintf(msg,msg_size,"Could not save the update."); return -1; }
    fb.f=f;fb.total=0;
    c=easy_new(man.url,120L);
    if(!c) { fclose(f); remove(staging); snprintf(msg,msg_size,"Download failed."); return -1; }
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,file_write);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&fb);
    if(fetch_ok(c)) { curl_easy_cleanup(c); fclose(f); remove(staging); snprintf(msg,msg_size,"Download failed."); return -1; }
    curl_easy_cleanup(c);
    fclose(f);
    if(update_sha256_file(staging,hex) || strcasecmp(hex,man.sha256)) {
        remove(staging); snprintf(msg,msg_size,"Download failed the integrity check."); return -1;
    }
    /* Copy to a temporary name first so a failed backup never destroys
     * the previous EBOOT.BIN.bak. */
    if(copy_file(current,tmpbak)) { remove(staging); snprintf(msg,msg_size,"Could not back up the current version."); return -1; }
    if(rename(tmpbak,backup)) { remove(tmpbak); remove(staging); snprintf(msg,msg_size,"Could not back up the current version."); return -1; }
    if(rename(staging,current)) { remove(staging); snprintf(msg,msg_size,"Could not install the update."); return -1; }
    snprintf(msg,msg_size,"Update %s installed. Restart Neon Lounge.",man.version);
    return 1;
}
#else
int update_net_init(void) { return -1; }
void update_net_shutdown(void) {}
int update_check_and_install(const char *assets_dir,char *msg,size_t msg_size) {
    (void)assets_dir;
    if(msg && msg_size) snprintf(msg,msg_size,"Updates are only available on PS3.");
    return -1;
}
#endif
