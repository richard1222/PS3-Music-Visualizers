#include "podcast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ================= pure helpers ================= */

static void xml_unescape(char *s) {
    char *r=s,*w=s;
    while(*r) {
        if(*r=='&') {
            if(!strncmp(r,"&amp;",5)) { *w++='&'; r+=5; continue; }
            if(!strncmp(r,"&lt;",4)) { *w++='<'; r+=4; continue; }
            if(!strncmp(r,"&gt;",4)) { *w++='>'; r+=4; continue; }
            if(!strncmp(r,"&quot;",6)) { *w++='"'; r+=6; continue; }
            if(!strncmp(r,"&apos;",6)) { *w++='\''; r+=6; continue; }
            if(r[1]=='#') {
                char *end; long v;
                if(r[2]=='x' || r[2]=='X') v=strtol(r+3,&end,16);
                else v=strtol(r+2,&end,10);
                if(end && *end==';' && v>0 && v<128) { *w++=(char)v; r=end+1; continue; }
            }
        }
        *w++=*r++;
    }
    *w=0;
}
static void clean_text(char *s) {
    /* Strip a CDATA wrapper and surrounding whitespace, unescape. */
    char *p=s;
    while(*p==' ' || *p=='\t' || *p=='\r' || *p=='\n') p++;
    if(p!=s) memmove(s,p,strlen(p)+1);
    if(!strncmp(s,"<![CDATA[",9)) {
        size_t len=strlen(s);
        if(len>=12 && !strcmp(s+len-3,"]]>")) { s[len-3]=0; memmove(s,s+9,strlen(s+9)+1); }
    }
    size_t len=strlen(s);
    while(len && (s[len-1]==' ' || s[len-1]=='\t' || s[len-1]=='\r' || s[len-1]=='\n')) s[--len]=0;
    xml_unescape(s);
}
/* Case-insensitive substring search, bounded by end (may be NULL). */
static const char *find_ci(const char *start,const char *end,const char *needle) {
    size_t nl=strlen(needle);
    if(!nl) return NULL;
    for(const char *p=start;*p && (!end || p<end);p++)
        if(!strncasecmp(p,needle,nl)) return p;
    return NULL;
}
/* Copy the text content of <tag>...</tag> within [start,end). */
static int tag_text(const char *start,const char *end,const char *tag,char *out,size_t out_size) {
    char open[64],close[64];
    snprintf(open,sizeof(open),"<%s",tag);
    snprintf(close,sizeof(close),"</%s>",tag);
    const char *o=find_ci(start,end,open);
    if(!o) return -1;
    const char *gt=strchr(o,'>');
    if(!gt || (end && gt>=end)) return -1;
    const char *c=find_ci(gt+1,end,close);
    if(!c) return -1;
    size_t len=(size_t)(c-(gt+1));
    if(len>=out_size) len=out_size-1;
    memcpy(out,gt+1,len); out[len]=0;
    clean_text(out);
    return 0;
}
/* Copy an attribute value from a tag starting at `tag_start`. */
static int tag_attr(const char *tag_start,const char *end,const char *name,char *out,size_t out_size) {
    const char *gt=NULL;
    if(end) { for(const char *p=tag_start;p<end;p++) if(*p=='>') { gt=p; break; } }
    else gt=strchr(tag_start,'>');
    if(!gt) gt=end;
    if(!gt) return -1;
    char pat[48];
    snprintf(pat,sizeof(pat),"%s=",name);
    const char *p=find_ci(tag_start,gt,pat);
    if(!p) return -1;
    p+=strlen(pat);
    char q=*p;
    if(q!='"' && q!='\'') return -1;
    p++;
    const char *e=strchr(p,q);
    if(!e || (gt && e>gt)) e=gt;
    if(!e) return -1;
    size_t len=(size_t)(e-p);
    if(len>=out_size) len=out_size-1;
    memcpy(out,p,len); out[len]=0;
    xml_unescape(out);
    return 0;
}

static long days_from_civil(int y,int m,int d) {
    y-=m<=2;
    long era=(y>=0?y:y-399)/400;
    long yoe=y-era*400;
    long doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
    long doe=yoe*365+yoe/4-yoe/100+doy;
    return era*146097+doe-719468;
}
long podcast_parse_pubdate(const char *text) {
    if(!text || !*text) return 0;
    static const char *months[]={"jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec"};
    /* ISO 8601: YYYY-MM-DD[T ]hh:mm:ss */
    if(isdigit((unsigned char)text[0]) && isdigit((unsigned char)text[1]) &&
       isdigit((unsigned char)text[2]) && isdigit((unsigned char)text[3]) && text[4]=='-') {
        int y=atoi(text),mo=atoi(text+5),d=atoi(text+8);
        int h=0,mi=0,s=0;
        const char *t=strchr(text,'T');
        if(!t) t=strchr(text,' ');
        if(t) sscanf(t+1,"%d:%d:%d",&h,&mi,&s);
        if(mo<1 || mo>12 || d<1 || d>31) return 0;
        return days_from_civil(y,mo,d)*86400L+h*3600+mi*60+s;
    }
    /* RFC 2822: [Thu, ]26 Nov 2026 09:00:00 +0000 */
    int month=0;
    const char *mp=NULL;
    for(int i=0;i<12;i++) {
        mp=find_ci(text,NULL,months[i]);
        if(mp) { month=i+1; break; }
    }
    if(!month) return 0;
    /* Day: the number right before the month name. */
    int day=0;
    for(const char *p=mp;p>text;) {
        p--;
        if(isdigit((unsigned char)*p)) { /* walk back over the number */
            const char *q=p;
            while(q>text && isdigit((unsigned char)q[-1])) q--;
            day=atoi(q);
            break;
        }
        if(*p==',') break;
    }
    int year=atoi(mp+3);
    if(year<100) year+=2000;
    int h=0,mi=0,s=0;
    const char *colon=strchr(mp,':');
    if(colon) { h=atoi(colon-2); sscanf(colon+1,"%d:%d",&mi,&s); }
    if(day<1 || day>31 || year<1970) return 0;
    return days_from_civil(year,month,day)*86400L+h*3600+mi*60+s;
}
int podcast_parse_duration(const char *text) {
    if(!text || !*text) return 0;
    while(*text==' ') text++;
    if(!strchr(text,':')) { double d=strtod(text,NULL); return d>0?(int)d:0; }
    int parts[3]={0,0,0},n=0;
    const char *p=text;
    while(n<3) {
        parts[n++]=atoi(p);
        p=strchr(p,':');
        if(!p) break;
        p++;
    }
    long total=n==3?(long)parts[0]*3600+parts[1]*60+parts[2]
              :n==2?(long)parts[0]*60+parts[1]
              :parts[0];
    return total>0?(int)total:0;
}
static int episode_playable(const char *type,const char *url) {
    if(type && *type) {
        if(!strncasecmp(type,"audio/mpeg",10) || !strncasecmp(type,"audio/mp3",9)) return 1;
        if(!strncasecmp(type,"audio/",6)) return 0;  /* m4a/mp4/aac: no decoder */
    }
    const char *q=strchr(url,'?');
    size_t len=q?(size_t)(q-url):strlen(url);
    if(len>=4 && !strncasecmp(url+len-4,".mp3",4)) return 1;
    if(len>=4 && (!strncasecmp(url+len-4,".m4a",4) || !strncasecmp(url+len-4,".mp4",4) ||
                  !strncasecmp(url+len-4,".aac",4))) return 0;
    return 1;  /* unknown: let the decoder judge, like radio does */
}
int podcast_parse_feed(const char *xml,PodEpisode *out,int max,char *show_title,size_t show_title_size) {
    if(!xml || !out || max<=0) return -1;
    /* Not a feed at all (an error page, a random document): the
     * header contract is -1 so callers can say "could not load"
     * instead of showing an empty episode list. */
    if(!find_ci(xml,NULL,"<channel") && !find_ci(xml,NULL,"<feed")) return -1;
    if(show_title && show_title_size) {
        show_title[0]=0;
        /* Channel title: the first <title> that is not inside an item. */
        const char *first_item=find_ci(xml,NULL,"<item");
        tag_text(xml,first_item,"title",show_title,show_title_size);
    }
    int count=0;
    const char *cursor=xml;
    while(count<max) {
        const char *item=find_ci(cursor,NULL,"<item");
        if(!item) break;
        const char *next=find_ci(item+5,NULL,"<item");
        const char *close=find_ci(item+5,NULL,"</channel>");
        const char *iend=next?next:(close?close:item+strlen(item));
        PodEpisode *ep=&out[count];
        memset(ep,0,sizeof(*ep));
        char type[64]="";
        const char *enc=find_ci(item,iend,"<enclosure");
        if(enc) {
            tag_attr(enc,iend,"url",ep->url,sizeof(ep->url));
            tag_attr(enc,iend,"type",type,sizeof(type));
            char lenbuf[32]="";
            if(!tag_attr(enc,iend,"length",lenbuf,sizeof(lenbuf))) ep->length=strtol(lenbuf,NULL,10);
        }
        if(!ep->url[0]) {
            /* Atom-ish fallback: <link rel="enclosure" href="..."/> */
            const char *lk=find_ci(item,iend,"rel=\"enclosure\"");
            if(!lk) lk=find_ci(item,iend,"rel='enclosure'");
            if(lk) {
                const char *ls=lk;
                while(ls>item && ls[-1]!='<') ls--;
                tag_attr(ls,iend,"href",ep->url,sizeof(ep->url));
                tag_attr(ls,iend,"type",type,sizeof(type));
            }
        }
        if(!ep->url[0]) { cursor=iend; continue; }  /* not an episode */
        tag_text(item,iend,"title",ep->title,sizeof(ep->title));
        tag_text(item,iend,"pubDate",ep->date,sizeof(ep->date));
        if(!ep->date[0]) tag_text(item,iend,"published",ep->date,sizeof(ep->date));
        ep->date_key=podcast_parse_pubdate(ep->date);
        char dur[32]="";
        if(!tag_text(item,iend,"itunes:duration",dur,sizeof(dur))) ep->duration_sec=podcast_parse_duration(dur);
        ep->playable=episode_playable(type,ep->url);
        count++;
        cursor=iend;
    }
    /* Newest first; the sort is stable, so undated episodes keep
     * their feed order below the dated ones. */
    for(int i=1;i<count;i++) {
        PodEpisode tmp=out[i];
        int j=i;
        while(j>0 && out[j-1].date_key<tmp.date_key) { out[j]=out[j-1]; j--; }
        out[j]=tmp;
    }
    return count;
}

/* --- iTunes search JSON --- */
static int json_value(const char *start,const char *end,const char *key,char *out,size_t out_size) {
    char pat[64];
    snprintf(pat,sizeof(pat),"\"%s\"",key);
    const char *p=find_ci(start,end,pat);
    if(!p) return -1;
    p=strchr(p,':');
    if(!p || (end && p>=end)) return -1;
    p++;
    while(*p==' ' || *p=='\t') p++;
    if(*p!='"') return -1;
    p++;
    size_t n=0;
    while(*p && *p!='"' && (!end || p<end)) {
        char c=*p;
        if(c=='\\' && p[1]) {
            p++;
            c=*p=='n'?'\n':(*p=='t'?'\t':*p);
            if(*p=='u') {  /* \uXXXX: keep ASCII, drop the rest */
                long v=strtol(p+1,NULL,16);
                c=(v>0 && v<128)?(char)v:'?';
                p+=4;
            }
        }
        if(n+1<out_size) out[n++]=c;
        p++;
    }
    out[n]=0;
    return 0;
}
int podcast_parse_search(const char *json,PodSearchResult *out,int max) {
    if(!json || !out || max<=0) return -1;
    int count=0;
    const char *cursor=json;
    while(count<max) {
        const char *f=find_ci(cursor,NULL,"\"feedUrl\"");
        if(!f) break;
        PodSearchResult *r=&out[count];
        memset(r,0,sizeof(*r));
        if(json_value(f,NULL,"feedUrl",r->feed_url,sizeof(r->feed_url)) || !r->feed_url[0]) { cursor=f+9; continue; }
        /* Name/artist live in the same result object, before feedUrl:
         * take the last occurrence inside this object's window. */
        const char *win=cursor;
        const char *last=NULL,*p;
        p=win;
        while((p=find_ci(p,f,"\"collectionName\""))) { last=p; p+=2; }
        if(last) json_value(last,f,"collectionName",r->name,sizeof(r->name));
        last=NULL; p=win;
        while((p=find_ci(p,f,"\"artistName\""))) { last=p; p+=2; }
        if(last) json_value(last,f,"artistName",r->artist,sizeof(r->artist));
        int dup=0;
        for(int i=0;i<count;i++) if(!strcmp(out[i].feed_url,r->feed_url)) dup=1;
        if(!dup) count++;
        cursor=f+9;
    }
    return count;
}
void podcast_url_encode(const char *in,char *out,size_t out_size) {
    static const char hex[]="0123456789ABCDEF";
    size_t n=0;
    if(!out || !out_size) return;
    out[0]=0;
    if(!in) return;
    for(const unsigned char *p=(const unsigned char*)in;*p;p++) {
        char c;
        if((*p>='A' && *p<='Z') || (*p>='a' && *p<='z') || (*p>='0' && *p<='9') ||
           *p=='-' || *p=='_' || *p=='.' || *p=='~') {
            if(n+2>out_size) break;
            out[n++]=(char)*p; out[n]=0;
        } else if(*p==' ') {
            if(n+2>out_size) break;
            out[n++]='+'; out[n]=0;
        } else {
            if(n+4>out_size) break;
            c='%'; out[n++]=c; out[n++]=hex[*p>>4]; out[n++]=hex[*p&15]; out[n]=0;
        }
    }
}
void podcast_cache_name(const char *url,char *out,size_t out_size) {
    unsigned long h=2166136261UL;
    if(url) for(const unsigned char *p=(const unsigned char*)url;*p;p++) { h^=*p; h*=16777619UL; }
    snprintf(out,out_size,"podcache/%08lx.mp3",h&0xffffffffUL);
}

/* --- persistence --- */
static void trim_line(char *s) {
    size_t len=strlen(s);
    while(len && (s[len-1]=='\n' || s[len-1]=='\r' || s[len-1]==' ' || s[len-1]=='\t')) s[--len]=0;
}
int podcast_subs_load(const char *path,PodSub *subs,int max) {
    FILE *f=fopen(path,"r");
    if(!f) return 0;
    int count=0;
    char line[768];
    while(count<max && fgets(line,sizeof(line),f)) {
        trim_line(line);
        if(!line[0] || line[0]=='#') continue;
        PodSub *s=&subs[count];
        memset(s,0,sizeof(*s));
        char *tab=strchr(line,'\t');
        if(tab) {
            *tab=0;
            snprintf(s->title,sizeof(s->title),"%.*s",(int)sizeof(s->title)-1,line);
            snprintf(s->url,sizeof(s->url),"%.*s",(int)sizeof(s->url)-1,tab+1);
        } else {
            snprintf(s->url,sizeof(s->url),"%.*s",(int)sizeof(s->url)-1,line);
        }
        if(strncmp(s->url,"http",4)) continue;
        count++;
    }
    fclose(f);
    return count;
}
int podcast_subs_save(const char *path,const PodSub *subs,int count) {
    FILE *f=fopen(path,"w");
    if(!f) return -1;
    for(int i=0;i<count;i++) {
        if(subs[i].title[0]) fprintf(f,"%s\t%s\n",subs[i].title,subs[i].url);
        else fprintf(f,"%s\n",subs[i].url);
    }
    fclose(f);
    return 0;
}
int podcast_pos_load(const char *path,PodPosStore *store) {
    store->count=0;
    FILE *f=fopen(path,"r");
    if(!f) return 0;
    char line[640];
    while(store->count<POD_POS_MAX && fgets(line,sizeof(line),f)) {
        trim_line(line);
        char *t1=strchr(line,'\t');
        if(!t1) continue;
        char *t2=strchr(t1+1,'\t');
        if(!t2) continue;
        *t1=0; *t2=0;
        PodPos *p=&store->items[store->count];
        p->seconds=atoi(line);
        /* The file is user-editable and could be corrupt: clamp the
         * position to a week so the resume byte math in main.c can
         * never overflow or seek somewhere absurd. */
        if(p->seconds<0) p->seconds=0;
        if(p->seconds>7*86400) p->seconds=7*86400;
        p->played=atoi(t1+1)?1:0;
        snprintf(p->url,sizeof(p->url),"%s",t2+1);
        if(!p->url[0]) continue;
        store->count++;
    }
    fclose(f);
    return store->count;
}
int podcast_pos_save(const char *path,const PodPosStore *store) {
    FILE *f=fopen(path,"w");
    if(!f) return -1;
    for(int i=0;i<store->count;i++)
        fprintf(f,"%d\t%d\t%s\n",store->items[i].seconds,store->items[i].played,store->items[i].url);
    fclose(f);
    return 0;
}
const PodPos *podcast_pos_find(const PodPosStore *store,const char *url) {
    if(!store || !url) return NULL;
    for(int i=0;i<store->count;i++)
        if(!strcmp(store->items[i].url,url)) return &store->items[i];
    return NULL;
}
void podcast_pos_set(PodPosStore *store,const char *url,int seconds,int played) {
    if(!store || !url || !*url) return;
    for(int i=0;i<store->count;i++) {
        if(!strcmp(store->items[i].url,url)) {
            if(seconds>=0) store->items[i].seconds=seconds;
            if(played>=0) store->items[i].played=played?1:0;
            return;
        }
    }
    if(store->count>=POD_POS_MAX) return;
    PodPos *p=&store->items[store->count++];
    snprintf(p->url,sizeof(p->url),"%s",url);
    p->seconds=seconds>0?seconds:0;
    if(p->seconds>7*86400) p->seconds=7*86400;
    p->played=played>0?1:0;
}

#ifdef NEON_PS3
/* ================= network (PS3) ================= */
#include <SDL.h>
#include <curl/curl.h>

typedef struct { char *buf; size_t len,alloc,cap; int overflow; } FetchBuf;
static size_t fetch_receive(void *data,size_t size,size_t n,void *p) {
    FetchBuf *f=p;
    size_t bytes=size*n;
    if(f->len+bytes>f->cap) { f->overflow=1; return 0; }
    if(f->len+bytes+1>f->alloc) {
        size_t na=(f->len+bytes+1)*2;
        char *nb=realloc(f->buf,na);
        if(!nb) { f->overflow=1; return 0; }
        f->buf=nb; f->alloc=na;
    }
    memcpy(f->buf+f->len,data,bytes);
    f->len+=bytes;
    f->buf[f->len]=0;
    return bytes;
}
static char *fetch_url(const char *url,const char *ca_path,size_t cap,long timeout) {
    FetchBuf f;
    memset(&f,0,sizeof(f));
    f.cap=cap;
    f.buf=malloc(65537);
    if(!f.buf) return NULL;
    f.alloc=65537; f.buf[0]=0;
    CURL *c=curl_easy_init();
    if(!c) { free(f.buf); return NULL; }
    curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_PROXY,"");
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,(long)(CURLPROTO_HTTP|CURLPROTO_HTTPS));
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_MAXREDIRS,10L);
    curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,8L);
    curl_easy_setopt(c,CURLOPT_TIMEOUT,timeout);
    curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(c,CURLOPT_USERAGENT,"NeonLounge/1.20");
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,fetch_receive);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&f);
    if(ca_path && *ca_path) {
        curl_easy_setopt(c,CURLOPT_CAINFO,ca_path);
        curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
        curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
    }
    CURLcode rc=curl_easy_perform(c);
    curl_easy_cleanup(c);
    if(rc!=CURLE_OK || f.overflow || !f.len) { free(f.buf); return NULL; }
    return f.buf;
}
int podcast_fetch_feed(const char *url,const char *ca_path,PodEpisode *out,int max,char *show_title,size_t show_title_size) {
    char *xml=fetch_url(url,ca_path,4*1024*1024,120L);
    if(!xml) return -1;
    int n=podcast_parse_feed(xml,out,max,show_title,show_title_size);
    free(xml);
    return n;
}
int podcast_search(const char *term,const char *ca_path,PodSearchResult *out,int max) {
    char enc[384],url[768];
    podcast_url_encode(term,enc,sizeof(enc));
    if(snprintf(url,sizeof(url),"https://itunes.apple.com/search?term=%s&entity=podcast&limit=25",enc)>=(int)sizeof(url)) return -1;
    char *json=fetch_url(url,ca_path,512*1024,30L);
    if(!json) return -1;
    int n=podcast_parse_search(json,out,max);
    free(json);
    return n;
}

/* --- stream engine: curl thread -> cache file -> decoder reads ---
 * Status codes mirror radio: 0 stopped, 1 connecting, 2 downloading,
 * 3 ready, 4 complete, -1 failed. */
#define POD_START_BYTES (48*1024)
static SDL_Thread *pod_worker;
static SDL_atomic_t pod_cancel,pod_state;
static char pod_url[512];
static char pod_cache[768];
static char pod_ca[768];
static FILE *pod_write_fp,*pod_read_fp;
static volatile long pod_downloaded,pod_total_bytes,pod_read_pos;
static long pod_resume_offset;
static int pod_partial;         /* response was 206 to our Range ask */
static int pod_restart;         /* server ignored Range: truncate */
static size_t pod_header(void *data,size_t size,size_t n,void *p) {
    (void)p;
    size_t bytes=size*n;
    const char *line=data;
    if(bytes>5 && !strncasecmp(line,"HTTP/",5)) {
        const char *sp=strchr(line,' ');
        if(sp) {
            int code=atoi(sp+1);
            if(code==206) pod_partial=1;
            else if(code==200 && pod_resume_offset>0 && !pod_partial) pod_restart=1;
        }
    }
    return bytes;
}
static int pod_progress(void *p,curl_off_t dltotal,curl_off_t dlnow,curl_off_t a,curl_off_t b) {
    (void)p;(void)a;(void)b;
    if(dltotal>0) pod_total_bytes=pod_partial?pod_resume_offset+(long)dltotal:(long)dltotal;
    (void)dlnow;
    return SDL_AtomicGet(&pod_cancel);
}
static size_t pod_receive(void *data,size_t size,size_t n,void *p) {
    (void)p;
    size_t bytes=size*n;
    if(SDL_AtomicGet(&pod_cancel)) return 0;
    if(pod_restart) {
        /* The server ignored our Range request and is sending the
         * whole file: start the cache over from this chunk. */
        pod_restart=0;
        pod_partial=0;
        if(pod_write_fp) { fclose(pod_write_fp); pod_write_fp=NULL; }
        pod_write_fp=fopen(pod_cache,"wb");
        if(!pod_write_fp) return 0;
        pod_downloaded=0;
        pod_resume_offset=0;
    }
    if(!pod_write_fp) return 0;
    size_t wrote=fwrite(data,1,bytes,pod_write_fp);
    fflush(pod_write_fp);  /* the decoder reads through a second handle */
    pod_downloaded+=wrote;
    if(pod_downloaded>0 && SDL_AtomicGet(&pod_state)<2) SDL_AtomicSet(&pod_state,2);
    if(pod_downloaded>=POD_START_BYTES) SDL_AtomicSet(&pod_state,3);
    return wrote;
}
static int pod_run(void *p) {
    (void)p;
    CURL *c=curl_easy_init();
    CURLcode rc=CURLE_FAILED_INIT;
    if(c) {
        curl_easy_setopt(c,CURLOPT_URL,pod_url);
        curl_easy_setopt(c,CURLOPT_PROXY,"");
        curl_easy_setopt(c,CURLOPT_PROTOCOLS,(long)(CURLPROTO_HTTP|CURLPROTO_HTTPS));
        curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(c,CURLOPT_MAXREDIRS,10L);
        curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
        curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,8L);
        curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(c,CURLOPT_USERAGENT,"NeonLounge/1.20");
        curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,pod_receive);
        curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,pod_header);
        curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,pod_progress);
        curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
        if(pod_resume_offset>0)
            curl_easy_setopt(c,CURLOPT_RESUME_FROM_LARGE,(curl_off_t)pod_resume_offset);
        if(pod_ca[0]) {
            curl_easy_setopt(c,CURLOPT_CAINFO,pod_ca);
            curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
            curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
        }
        rc=curl_easy_perform(c);
        curl_easy_cleanup(c);
    }
    if(pod_write_fp) { fclose(pod_write_fp); pod_write_fp=NULL; }
    if(SDL_AtomicGet(&pod_cancel)) SDL_AtomicSet(&pod_state,0);
    else if(rc==CURLE_OK && pod_downloaded>0) SDL_AtomicSet(&pod_state,4);
    else SDL_AtomicSet(&pod_state,-1);
    return 0;
}
int podcast_stream_start(const char *url,const char *cache_path,const char *ca_path) {
    podcast_stream_stop();
    if(!url || !*url || !cache_path || !*cache_path) return -1;
    if(snprintf(pod_url,sizeof(pod_url),"%s",url)>=(int)sizeof(pod_url)) return -1;
    if(snprintf(pod_cache,sizeof(pod_cache),"%s",cache_path)>=(int)sizeof(pod_cache)) return -1;
    if(ca_path) snprintf(pod_ca,sizeof(pod_ca),"%s",ca_path); else pod_ca[0]=0;
    /* Resume from whatever a previous session left on disk. */
    pod_resume_offset=0;
    FILE *probe=fopen(pod_cache,"rb");
    if(probe) { fseek(probe,0,SEEK_END); long sz=ftell(probe); fclose(probe); if(sz>0) pod_resume_offset=sz; }
    pod_downloaded=pod_resume_offset;
    pod_total_bytes=0;
    pod_read_pos=0;
    pod_partial=0;
    pod_restart=0;
    pod_write_fp=fopen(pod_cache,pod_resume_offset>0?"ab":"wb");
    if(!pod_write_fp) return -1;
    SDL_AtomicSet(&pod_cancel,0);
    SDL_AtomicSet(&pod_state,1);
    pod_worker=SDL_CreateThread(pod_run,"Podcast download",NULL);
    if(!pod_worker) { podcast_stream_stop(); return -1; }
    return 0;
}
void podcast_stream_stop(void) {
    SDL_AtomicSet(&pod_cancel,1);
    if(pod_worker) SDL_WaitThread(pod_worker,NULL);
    pod_worker=NULL;
    if(pod_write_fp) { fclose(pod_write_fp); pod_write_fp=NULL; }
    if(pod_read_fp) { fclose(pod_read_fp); pod_read_fp=NULL; }
    SDL_AtomicSet(&pod_state,0);
}
int podcast_stream_status(void) { return SDL_AtomicGet(&pod_state); }
long podcast_downloaded(void) { return pod_downloaded; }
long podcast_total(void) { return pod_total_bytes; }
int podcast_download_complete(void) { return SDL_AtomicGet(&pod_state)==4; }
int podcast_download_terminal(void) { int s=SDL_AtomicGet(&pod_state); return s==4 || s<0; }
size_t podcast_read(void *dst,size_t want) {
    if(!dst || !want) return 0;
    if(!pod_read_fp) {
        pod_read_fp=fopen(pod_cache,"rb");
        if(!pod_read_fp) return 0;
        pod_read_pos=0;
    }
    /* Like radio_read: a short wait on underrun, so the decoder does
     * not latch EOF on a download hiccup. Once the download thread
     * reports complete/failed the EOF is real and returns at once.
     * clearerr matters here: a growing file keeps hitting its EOF
     * flag, which would otherwise pin every later read at 0. */
    for(int tries=0;tries<30;tries++) {
        clearerr(pod_read_fp);
        size_t got=fread(dst,1,want,pod_read_fp);
        if(got>0) { pod_read_pos+=(long)got; return got; }
        int st=SDL_AtomicGet(&pod_state);
        if(st==4 || st<0 || SDL_AtomicGet(&pod_cancel)) return 0;
        SDL_Delay(10);
    }
    return 0;
}
int podcast_stream_seek(long position) {
    if(position<0) position=0;
    if(position>pod_downloaded) return -1;
    if(!pod_read_fp) {
        pod_read_fp=fopen(pod_cache,"rb");
        if(!pod_read_fp) return -1;
        pod_read_pos=0;
    }
    if(fseek(pod_read_fp,position,SEEK_SET)) return -1;
    clearerr(pod_read_fp);
    pod_read_pos=position;
    return 0;
}
#endif /* NEON_PS3 */
