#ifndef NEON_PODCAST_H
#define NEON_PODCAST_H
#include <stddef.h>
/* Podcasts (v1.20): RSS subscriptions, episode lists, iTunes search,
 * resume positions and a file-backed stream engine.
 *
 * The parse/persistence half of podcast.c is plain C with no SDL or
 * curl in it, so the host tests exercise it directly: RSS parsing,
 * subscription and position files, iTunes JSON parsing. The network
 * half (feed fetch, search, the download engine) is PS3-only, in the
 * radio.c shape: a curl thread fills storage, the shared minimp3
 * decoder reads through podcast_read, so EQ, spectrum, the sleep
 * timer and play counts treat an episode like any other source.
 *
 * Unlike radio's dropping ring, the podcast engine downloads into a
 * per-episode cache file (USRDIR/podcache/<hash>.mp3) and resumes
 * with an HTTP Range request when a partial file exists - episodes
 * are long, and a dropped live ring cannot seek. Servers that ignore
 * Range answer 200 and the engine restarts that file from zero. */
#define POD_SUB_MAX 40
#define POD_EP_MAX 200
#define POD_POS_MAX 200
#define POD_SEARCH_MAX 25

typedef struct {
    char title[192];
    char date[40];      /* pubDate as written in the feed */
    long date_key;      /* parsed, for newest-first ordering */
    int duration_sec;   /* itunes:duration, 0 when absent */
    char url[512];      /* enclosure URL, entities unescaped */
    long length;        /* enclosure length bytes, 0 when absent */
    int playable;       /* 0 for AAC/.m4a enclosures (no AAC decoder) */
} PodEpisode;

typedef struct { char title[128]; char url[512]; } PodSub;
typedef struct { char name[160]; char artist[128]; char feed_url[512]; } PodSearchResult;
typedef struct { char url[512]; int seconds; int played; } PodPos;
typedef struct { PodPos items[POD_POS_MAX]; int count; } PodPosStore;

/* --- pure helpers (host-tested) --- */
/* Parse an RSS/Atom-ish feed body. Returns the episode count
 * (newest first), or -1 when the text holds no channel at all.
 * show_title receives the channel title when a buffer is given. */
int podcast_parse_feed(const char *xml,PodEpisode *out,int max,char *show_title,size_t show_title_size);
int podcast_parse_duration(const char *text);   /* "1:02:03" / "62:03" / "3600" -> seconds, 0 on junk */
long podcast_parse_pubdate(const char *text);   /* RFC 2822 or ISO 8601 -> comparable key, 0 on junk */
/* Parse an iTunes Search API response (entity=podcast). */
int podcast_parse_search(const char *json,PodSearchResult *out,int max);
void podcast_url_encode(const char *in,char *out,size_t out_size);
/* "podcache/1a2b3c4d.mp3" - stable per-URL cache file name. */
void podcast_cache_name(const char *url,char *out,size_t out_size);

/* --- persistence (host-tested) --- */
/* podcasts.txt lines: a bare feed URL, or "title<TAB>url" (what
 * podcast_subs_save writes once a title is known). */
int podcast_subs_load(const char *path,PodSub *subs,int max);
int podcast_subs_save(const char *path,const PodSub *subs,int count);
int podcast_pos_load(const char *path,PodPosStore *store);
int podcast_pos_save(const char *path,const PodPosStore *store);
const PodPos *podcast_pos_find(const PodPosStore *store,const char *url);
/* Insert or update; seconds<0 leaves the stored position alone,
 * played<0 leaves the played flag alone. */
void podcast_pos_set(PodPosStore *store,const char *url,int seconds,int played);

/* --- network (PS3) --- */
/* Blocking fetch of a feed: fills episodes (newest first) and the
 * show title. Returns the episode count or -1. */
int podcast_fetch_feed(const char *url,const char *ca_path,PodEpisode *out,int max,char *show_title,size_t show_title_size);
/* Blocking iTunes podcast search. Returns the result count or -1. */
int podcast_search(const char *term,const char *ca_path,PodSearchResult *out,int max);

/* --- stream engine (PS3), radio-shaped status codes ---
 * 0 stopped, 1 connecting, 2 downloading, 3 ready to decode,
 * 4 complete (whole episode on disk), -1 failed/ended short. */
int podcast_stream_start(const char *url,const char *cache_path,const char *ca_path);
void podcast_stream_stop(void);
int podcast_stream_status(void);
long podcast_downloaded(void);
long podcast_total(void);       /* 0 when the server did not say */
int podcast_download_complete(void);
/* 1 when the download will grow no further (complete or failed):
 * the decoder's EOF is real from that point on. */
int podcast_download_terminal(void);
size_t podcast_read(void *dst,size_t want);
int podcast_stream_seek(long position);  /* 0 ok, -1 beyond downloaded */
#endif
