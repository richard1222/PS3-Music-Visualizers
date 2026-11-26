#define _DEFAULT_SOURCE
#include "podcast.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *slurp(const char *path) {
    FILE *f=fopen(path,"rb"); assert(f);
    assert(fseek(f,0,SEEK_END)==0);
    long n=ftell(f); assert(n>0);
    assert(fseek(f,0,SEEK_SET)==0);
    char *buf=malloc((size_t)n+1); assert(buf);
    assert(fread(buf,1,(size_t)n,f)==(size_t)n);
    buf[n]=0; fclose(f);
    return buf;
}

int main(void) {
    static PodEpisode eps[POD_EP_MAX];
    char title[256];

    /* --- real captured feeds (Step 3 live verification) --- */
    char *npr=slurp("tests/fixtures/feed_npr.xml");
    int n=podcast_parse_feed(npr,eps,POD_EP_MAX,title,sizeof(title));
    assert(n>0);
    assert(strstr(title,"NPR")!=NULL);
    assert(eps[0].playable);
    assert(strncmp(eps[0].url,"http",4)==0);
    assert(strstr(eps[0].url,"&amp;")==NULL);   /* entities unescaped */
    assert(eps[0].date_key>0);
    for(int i=1;i<n;i++) assert(eps[i-1].date_key>=eps[i].date_key); /* newest first */
    free(npr);

    char *tal=slurp("tests/fixtures/feed_tal.xml");
    n=podcast_parse_feed(tal,eps,POD_EP_MAX,title,sizeof(title));
    assert(n>0);
    assert(eps[0].playable);
    free(tal);

    /* A truncated feed (first 48 KB of a ~1 MB one) must parse what
     * is complete and never crash or overrun. */
    char *dn=slurp("tests/fixtures/feed_darknet_head.xml");
    n=podcast_parse_feed(dn,eps,POD_EP_MAX,title,sizeof(title));
    assert(n>=1);
    free(dn);

    /* --- inline edge cases --- */
    const char *aac_feed=
        "<rss><channel><title>AAC Show</title>"
        "<item><title>Ep &amp; One</title>"
        "<pubDate>Mon, 24 Nov 2026 10:00:00 GMT</pubDate>"
        "<enclosure url=\"https://example.com/ep1.m4a\" length=\"123\" type=\"audio/x-m4a\"/>"
        "</item>"
        "<item><title><![CDATA[Second <Ep>]]></title>"
        "<pubDate>Tue, 25 Nov 2026 10:00:00 GMT</pubDate>"
        "<itunes:duration>1:02:03</itunes:duration>"
        "<enclosure type=\"audio/mpeg\" length=\"456\" url=\"https://example.com/ep2.mp3?a=1&amp;b=2\"/>"
        "</item></channel></rss>";
    n=podcast_parse_feed(aac_feed,eps,POD_EP_MAX,title,sizeof(title));
    assert(n==2);
    assert(!strcmp(title,"AAC Show"));
    /* newest first: the Nov 25 MP3 episode leads */
    assert(eps[0].playable);
    assert(strstr(eps[0].url,"ep2.mp3")!=NULL);
    assert(strstr(eps[0].url,"a=1&b=2")!=NULL);
    assert(eps[0].duration_sec==3723);
    assert(!strcmp(eps[0].title,"Second <Ep>"));   /* CDATA unwrapped */
    assert(!eps[1].playable);                      /* AAC marked unplayable */
    assert(!strcmp(eps[1].title,"Ep & One"));      /* entity in title */

    assert(podcast_parse_feed("not xml at all",eps,POD_EP_MAX,title,sizeof(title))==-1);
    assert(podcast_parse_feed("<rss><channel></channel></rss>",eps,POD_EP_MAX,title,sizeof(title))==0);

    /* --- duration / pubdate helpers --- */
    assert(podcast_parse_duration("1:02:03")==3723);
    assert(podcast_parse_duration("62:03")==3723);
    assert(podcast_parse_duration("3600")==3600);
    assert(podcast_parse_duration("junk")==0);
    assert(podcast_parse_duration("")==0);
    assert(podcast_parse_duration("1:99:99")>0);   /* tolerated, not trusted */
    long d1=podcast_parse_pubdate("Mon, 24 Nov 2026 10:00:00 GMT");
    long d2=podcast_parse_pubdate("Tue, 25 Nov 2026 10:00:00 GMT");
    assert(d1>0 && d2>d1);
    assert(podcast_parse_pubdate("2026-11-25T10:00:00Z")>=d2);
    assert(podcast_parse_pubdate("garbage")==0);

    /* --- subscriptions round-trip --- */
    char dir[]="/tmp/neon-pod-XXXXXX"; assert(mkdtemp(dir));
    char path[512]; snprintf(path,sizeof(path),"%s/podcasts.txt",dir);
    PodSub subs[3]; memset(subs,0,sizeof(subs));
    snprintf(subs[0].url,sizeof(subs[0].url),"https://example.com/a.xml");
    snprintf(subs[1].title,sizeof(subs[1].title),"My Show");
    snprintf(subs[1].url,sizeof(subs[1].url),"https://example.com/b.xml");
    assert(podcast_subs_save(path,subs,2)==0);
    PodSub back[POD_SUB_MAX];
    int cnt=podcast_subs_load(path,back,POD_SUB_MAX);
    assert(cnt==2);
    assert(!strcmp(back[0].url,"https://example.com/a.xml") && !back[0].title[0]);
    assert(!strcmp(back[1].url,"https://example.com/b.xml") && !strcmp(back[1].title,"My Show"));
    /* a hand-written bare-URL file loads too */
    FILE *f=fopen(path,"w"); assert(f);
    fputs("https://example.com/c.xml\n\n# not a url\n",f); fclose(f);
    cnt=podcast_subs_load(path,back,POD_SUB_MAX);
    assert(cnt==1 && !strcmp(back[0].url,"https://example.com/c.xml"));

    /* --- positions round-trip + corrupt lines --- */
    char ppath[512]; snprintf(ppath,sizeof(ppath),"%s/positions.txt",dir);
    PodPosStore store; memset(&store,0,sizeof(store));
    podcast_pos_set(&store,"https://example.com/ep1.mp3",754,0);
    podcast_pos_set(&store,"https://example.com/ep2.mp3",0,1);
    podcast_pos_set(&store,"https://example.com/ep1.mp3",900,-1);  /* move, keep flag */
    assert(store.count==2);
    const PodPos *pp=podcast_pos_find(&store,"https://example.com/ep1.mp3");
    assert(pp && pp->seconds==900 && pp->played==0);
    assert(podcast_pos_save(ppath,&store)==0);
    PodPosStore back2; memset(&back2,0,sizeof(back2));
    assert(podcast_pos_load(ppath,&back2)==2);
    pp=podcast_pos_find(&back2,"https://example.com/ep2.mp3");
    assert(pp && pp->played==1);
    assert(podcast_pos_find(&back2,"https://example.com/nope.mp3")==NULL);
    /* corrupt/partial lines are skipped, good lines survive */
    f=fopen(ppath,"w"); assert(f);
    fputs("garbage line\n123\tx\thttps://example.com/ok.mp3\n-5\t0\thttps://example.com/neg.mp3\n999999999\t0\thttps://example.com/huge.mp3\n",f); fclose(f);
    memset(&back2,0,sizeof(back2));
    podcast_pos_load(ppath,&back2);
    pp=podcast_pos_find(&back2,"https://example.com/ok.mp3");
    assert(pp && pp->seconds==123);
    /* out-of-range positions are clamped, never trusted raw:
     * the resume byte math multiplies seconds by the bitrate */
    pp=podcast_pos_find(&back2,"https://example.com/neg.mp3");
    assert(pp && pp->seconds==0);
    pp=podcast_pos_find(&back2,"https://example.com/huge.mp3");
    assert(pp && pp->seconds==7*86400);
    assert(podcast_parse_duration("-30")==0);
    assert(podcast_parse_duration("-1:30")==0);

    /* --- iTunes search JSON --- */
    const char *json="{\"resultCount\":2,\"results\":["
        "{\"collectionName\":\"Darknet Diaries\",\"artistName\":\"Jack Rhysider\","
        "\"feedUrl\": \"https://podcast.darknetdiaries.com\"},"
        "{\"collectionName\":\"No Feed Show\",\"artistName\":\"Nobody\"}]}";
    static PodSearchResult res[POD_SEARCH_MAX];
    int rn=podcast_parse_search(json,res,POD_SEARCH_MAX);
    assert(rn==1);   /* the result without a feedUrl is dropped */
    assert(!strcmp(res[0].name,"Darknet Diaries"));
    assert(!strcmp(res[0].feed_url,"https://podcast.darknetdiaries.com"));
    assert(podcast_parse_search("{}",res,POD_SEARCH_MAX)==0);
    assert(podcast_parse_search("junk",res,POD_SEARCH_MAX)<=0);

    /* --- url encode + cache name --- */
    char enc[128]; podcast_url_encode("a b&c/d?",enc,sizeof(enc));
    assert(!strcmp(enc,"a+b%26c%2Fd%3F"));   /* form encoding: space is + */
    char c1[64],c2[64];
    podcast_cache_name("https://example.com/ep1.mp3",c1,sizeof(c1));
    podcast_cache_name("https://example.com/ep1.mp3",c2,sizeof(c2));
    assert(!strcmp(c1,c2));                                   /* stable */
    assert(strncmp(c1,"podcache/",9)==0 && strstr(c1,".mp3")!=NULL);
    podcast_cache_name("https://example.com/ep2.mp3",c2,sizeof(c2));
    assert(strcmp(c1,c2));                                    /* distinct */
    assert(strstr(c1,"..")==NULL);                            /* no traversal */

    assert(!unlink(path)); assert(!unlink(ppath)); assert(!rmdir(dir));
    printf("test_podcast: OK\n");
    return 0;
}
