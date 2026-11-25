/* video-source.c - seekable, read-ahead reader over a local file or
 * a streamed url. See video-source.h for the design credit. The
 * read-ahead layer below follows upstream's video-source.c; the
 * backends are Neon Lounge's: stdio for files, and for http(s) a
 * prefetch thread filling a 4 MB ring through libcurl, shaped by
 * googlevideo's 2026 rules as measured by mohasi (http-stream.c):
 * ranges travel in the URL query ("&range=start-end"), one request
 * asks for at most ~4 s of media (4 MB cap), requests go strictly
 * forward, and a 403 means the address's small request budget is
 * spent - latched as an error, never retried against that address.
 * PLAYBACK3 fix 1 (revised after the ViTube survey, TRIAGE.md):
 * the window shrank 10 s -> 4 s so a window's bytes land as a
 * ~0.6 MB burst instead of 1.4 MB, and the prefetch thread runs
 * BELOW the main and player threads (see prefetchRun) so a
 * window's mbedTLS decrypt + ring copies can never preempt
 * presentation or audio. Handles stay FRESH per window, exactly
 * as in 1.14 - deliberately: ViTube measured that on a reused
 * handle googlevideo answered 404 for the first media fetch
 * after the playlist and then for every subsequent fetch on
 * that playlist, while fresh handles never 404'd. A persistent-
 * handle version of this fix was built and reverted before it
 * ever shipped; do not re-propose connection reuse without
 * watching for that 404-wave signature. */
#include "video-source.h"
#include <string.h>
#include <stdlib.h>

#define SOURCE_BUFFER_SIZE  65536   /* read-ahead window; container headers are read a few bytes at a time */

/* ==================================================================== */
/* http backend (PS3 only: curl + an SDL prefetch thread)               */
/* ==================================================================== */
#ifdef NEON_PS3

#include <SDL.h>
#include <strings.h>
#include <curl/curl.h>
#include <sys/thread.h>   /* sysThreadGetId / sysThreadSetPriority (prefetch priority drop) */
#include "update_tls.h"
#include "yt_net.h"      /* yt_net_ca_path() */
#include "youtube.h"     /* yt_log_sink() */

#define HTTP_RING_SIZE      (4 * 1024 * 1024)
#define HTTP_PREFETCH_AHEAD (2 * 1024 * 1024)   /* buffered-ahead cap (bytes) */
#define HTTP_WINDOW_MAX     (4 * 1024 * 1024)   /* one request's upper bound */
#define HTTP_WINDOW_SECONDS 4                   /* one request asks for this much media */
#define HTTP_HISTORY        (256 * 1024)        /* bytes kept behind the read position */
#define HTTP_EMPTY_LIMIT    3                   /* consecutive empty windows before latching an error */
#define HTTP_PREFETCH_PRIO  2000                /* lv2 priority of the prefetch threads: SDL
                                                 * creates its threads at 1500 and lower numbers
                                                 * win, so 2000 puts fetch work below the main
                                                 * thread and every player thread (PLAYBACK3) */

typedef struct {
   char     url[2560];
   int      queryRange;
   uint64_t size;            /* total resource size (0 = unknown) */
   uint64_t bytesPerSecond;  /* from clen/dur in the url, 0 = unknown */
   uint8_t *ring;
   uint64_t ringStart;       /* lowest offset still in the ring */
   uint64_t ringEnd;         /* one past the highest fetched byte */
   uint64_t readPos;         /* backend reader position */
   int      generation;      /* bumped on a window-dropping seek; stale fetches abort */
   volatile int running;
   volatile int atEof;
   volatile int error;       /* 0 none, 1 refused/spent, 2 other failure */
   volatile int aborted;     /* cancelVideoSource: readers fail fast */
   int      emptyWindows;
   int      requests;        /* diagnostic: windows requested so far */
   SDL_mutex  *lock;
   SDL_Thread *thread;
   volatile int *cancelFlag; /* owner's (may be NULL) */
} YtHttpBackend;

/* reads a numeric query parameter ("clen=") out of a url; 0 if absent */
static uint64_t queryNumber(const char *url, const char *parameter)
{
   const char *hit = strstr(url, parameter);
   if (!hit) return 0;
   uint64_t value = 0;
   for (const char *d = hit + strlen(parameter); *d >= '0' && *d <= '9'; d++)
      value = value * 10 + (uint64_t)(*d - '0');
   return value;
}

static int httpCancelled(YtHttpBackend *b)
{
   return !b->running || b->aborted || (b->cancelFlag && *b->cancelFlag);
}

/* ---- one window fetch (runs on the prefetch thread) ----------------- */

typedef struct {
   YtHttpBackend *b;
   uint64_t start;      /* absolute offset this window starts at */
   int      generation;
   uint64_t received;
   long     status;
   int      sawContentRange;
} WindowFetch;

static size_t windowWrite(void *ptr, size_t size, size_t nmemb, void *userdata)
{
   WindowFetch *f = (WindowFetch *)userdata;
   YtHttpBackend *b = f->b;
   size_t n = size * nmemb;
   size_t done = 0;
   while (done < n) {
      if (httpCancelled(b)) return 0;
      SDL_LockMutex(b->lock);
      if (f->generation != b->generation) { SDL_UnlockMutex(b->lock); return 0; }
      uint64_t at = f->start + f->received + done;
      if (at < b->ringStart || at > b->ringEnd) {   /* window moved under us: stale */
         SDL_UnlockMutex(b->lock);
         return 0;
      }
      uint64_t used = b->ringEnd - b->ringStart;
      if (used >= HTTP_RING_SIZE) {                  /* ring full: wait for the reader */
         SDL_UnlockMutex(b->lock);
         SDL_Delay(5);
         continue;
      }
      size_t slot = (size_t)(at % HTTP_RING_SIZE);
      size_t contiguous = HTTP_RING_SIZE - slot;
      size_t room = HTTP_RING_SIZE - (size_t)used;
      size_t take = n - done;
      if (take > contiguous) take = contiguous;
      if (take > room) take = room;
      memcpy(b->ring + slot, (const uint8_t *)ptr + done, take);
      if (at == b->ringEnd) b->ringEnd += take;      /* sequential window: extend the end */
      done += take;
      SDL_UnlockMutex(b->lock);
   }
   f->received += n;
   return n;
}

static size_t windowHeader(void *ptr, size_t size, size_t nmemb, void *userdata)
{
   WindowFetch *f = (WindowFetch *)userdata;
   size_t n = size * nmemb;
   /* Content-Range: bytes a-b/total - the fallback size source when the
    * url carries no clen (header-range hosts report it). */
   if (!f->sawContentRange && n > 15 && strncasecmp((const char *)ptr, "Content-Range:", 14) == 0) {
      const char *slash = strchr((const char *)ptr, '/');
      if (slash && slash[1] >= '0' && slash[1] <= '9') {
         uint64_t total = 0;
         for (const char *d = slash + 1; *d >= '0' && *d <= '9'; d++)
            total = total * 10 + (uint64_t)(*d - '0');
         if (total) {
            SDL_LockMutex(f->b->lock);
            if (!f->b->size) f->b->size = total;
            SDL_UnlockMutex(f->b->lock);
            f->sawContentRange = 1;
         }
      }
   }
   return n;
}

static int windowProgress(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
   (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
   return httpCancelled((YtHttpBackend *)clientp) ? 1 : 0;
}

/* fetches the window starting at b->ringEnd. Returns when the window is
 * served, refused, or the backend stops. Locks only around ring writes. */
static void fetchWindow(YtHttpBackend *b)
{
   SDL_LockMutex(b->lock);
   uint64_t start = b->ringEnd;
   uint64_t size = b->size;
   int generation = b->generation;
   SDL_UnlockMutex(b->lock);

   if (size && start >= size) {
      SDL_LockMutex(b->lock);
      b->atEof = 1;
      SDL_UnlockMutex(b->lock);
      return;
   }

   uint64_t window = b->bytesPerSecond ? b->bytesPerSecond * HTTP_WINDOW_SECONDS : HTTP_WINDOW_MAX;
   if (window > HTTP_WINDOW_MAX) window = HTTP_WINDOW_MAX;
   if (window == 0) window = HTTP_WINDOW_MAX;
   uint64_t end = start + window;
   if (size && end > size) end = size;

   char request[2816];
   char rangeHeader[64];
   struct curl_slist *headers = NULL;
   if (b->queryRange) {
      if (snprintf(request, sizeof(request), "%s&range=%llu-%llu", b->url,
                   (unsigned long long)start, (unsigned long long)(end - 1)) >= (int)sizeof(request))
         goto fail;
   } else {
      snprintf(request, sizeof(request), "%s", b->url);
      snprintf(rangeHeader, sizeof(rangeHeader), "Range: bytes=%llu-%llu",
               (unsigned long long)start, (unsigned long long)(end - 1));
      headers = curl_slist_append(headers, rangeHeader);
   }
   headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (PlayStation 3; NeonLounge)");

   const char *ca = yt_net_ca_path();
   /* Fresh handle per window, exactly as in 1.14 - see the file
    * header: ViTube's hardware evidence is that a reused handle
    * makes googlevideo 404 every media fetch for the life of the
    * playlist. The per-window TCP+TLS handshake cost is real but
    * is paid by this low-priority thread, segments ahead of the
    * read position (PLAYBACK3 fix 1, revised). */
   CURL *c = ca && *ca ? curl_easy_init() : NULL;
   if (!c) goto fail;
   WindowFetch f;
   memset(&f, 0, sizeof(f));
   f.b = b; f.start = start; f.generation = generation;
   curl_easy_setopt(c, CURLOPT_URL, request);
   curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
   curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, windowWrite);
   curl_easy_setopt(c, CURLOPT_WRITEDATA, &f);
   curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, windowHeader);
   curl_easy_setopt(c, CURLOPT_HEADERDATA, &f);
   curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
   curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
   curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
   curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 5L);
   curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, windowProgress);
   curl_easy_setopt(c, CURLOPT_XFERINFODATA, b);
   curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
   if (update_configure_tls(c, ca) != CURLE_OK) {
      curl_easy_cleanup(c);
      if (headers) { curl_slist_free_all(headers); headers = NULL; }
      goto fail;
   }
   /* ranged requests must not follow redirects (a redirect target drops
    * the range); the extractor's urls are direct. */
   curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);

   uint64_t fetch_t0 = SDL_GetPerformanceCounter();
   CURLcode rc = curl_easy_perform(c);
   uint64_t fetch_ms = (SDL_GetPerformanceCounter() - fetch_t0) * 1000ull
                       / SDL_GetPerformanceFrequency();
   curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &f.status);
   curl_easy_cleanup(c);
   if (headers) curl_slist_free_all(headers);
   /* Per-window fetch line (PLAYBACK3 fix 0): aligned against the
    * player's 1 Hz stats lines, this shows whether presentation
    * skips cluster inside window-fetch intervals. */
   {
      char line[224];
      snprintf(line, sizeof(line),
               "window start=%llu bytes=%llu in %llu ms (http %ld, rc %d)",
               (unsigned long long)start, (unsigned long long)f.received,
               (unsigned long long)fetch_ms, f.status, (int)rc);
      yt_log_sink("source", line);
   }

   SDL_LockMutex(b->lock);
   b->requests++;
   if (httpCancelled(b) || generation != b->generation) { SDL_UnlockMutex(b->lock); return; }
   if (f.status == 403 && b->queryRange) {
      /* the address has served its budget; retrying it can only hammer
       * a server that will never say yes (mohasi's measurement). */
      b->error = 1;
      yt_log_sink("source", "googlevideo address spent (403) - stream needs a fresh extract");
   } else if (rc != CURLE_OK && f.received == 0 && !httpCancelled(b)) {
      if (++b->emptyWindows >= HTTP_EMPTY_LIMIT) b->error = 2;
   } else if (f.status != 0 && f.status != 200 && f.status != 206) {
      b->error = 2;
   } else if (f.received == 0) {
      if (++b->emptyWindows >= HTTP_EMPTY_LIMIT) {
         if (b->size && b->ringEnd >= b->size) b->atEof = 1;
         else b->error = 2;
      }
   } else {
      b->emptyWindows = 0;
      if (b->size && b->ringEnd >= b->size) b->atEof = 1;
      /* a short window (server closed early) is spent too: the next
       * loop iteration opens the next window at ringEnd. */
   }
   SDL_UnlockMutex(b->lock);
   return;

fail:
   if (headers) curl_slist_free_all(headers);
   SDL_LockMutex(b->lock);
   if (!httpCancelled(b) && ++b->emptyWindows >= HTTP_EMPTY_LIMIT) b->error = 2;
   SDL_UnlockMutex(b->lock);
}

static int prefetchRun(void *arg)
{
   YtHttpBackend *b = (YtHttpBackend *)arg;
   /* Burst fix (PLAYBACK3 fix 1, revised): run the fetch work
    * BELOW the main thread and the player threads, so a window's
    * mbedTLS decrypt + ring copies fill scheduling gaps instead
    * of preempting presentation or audio delivery - ViTube's
    * structure (worker priority below main; the same bytes in
    * smaller, lower-priority fetches are background noise). SDL
    * creates every thread at lv2 priority 1500, so 2000 is
    * strictly below the feed, audio and UI threads. The lv2
    * calls are used directly because this libSDL2's
    * SDL_SetThreadPriority is a no-op: its SYS backend issues
    * syscall 48 (sysThreadGetPriority, pointer argument) where
    * set-priority is syscall 47 - verified in the shipped
    * libSDL2.a disassembly. The before/after readback is logged
    * so the hardware log proves the drop took effect. */
   {
      sys_ppu_thread_t tid = 0;
      s32 before = -1, after = -1, set_rc = -1;
      if (sysThreadGetId(&tid) == 0) {
         sysThreadGetPriority(tid, &before);
         set_rc = sysThreadSetPriority(tid, HTTP_PREFETCH_PRIO);
         sysThreadGetPriority(tid, &after);
      }
      char line[160];
      snprintf(line, sizeof(line), "prefetch priority %d -> %d (set rc=%d)",
               (int)before, (int)after, (int)set_rc);
      yt_log_sink("source", line);
   }
   while (b->running) {
      if (b->aborted || (b->cancelFlag && *b->cancelFlag)) { SDL_Delay(5); continue; }
      SDL_LockMutex(b->lock);
      int idle = b->error || b->atEof;
      uint64_t ahead = b->ringEnd >= b->readPos ? b->ringEnd - b->readPos : 0;
      uint64_t limit = b->bytesPerSecond ? b->bytesPerSecond * 20 : HTTP_PREFETCH_AHEAD;
      if (limit > HTTP_PREFETCH_AHEAD) limit = HTTP_PREFETCH_AHEAD;
      if (ahead >= limit && b->ringEnd > b->ringStart) idle = 1;
      if (b->size && b->ringEnd >= b->size) { b->atEof = 1; idle = 1; }
      SDL_UnlockMutex(b->lock);
      if (idle) { SDL_Delay(4); continue; }
      fetchWindow(b);
   }
   return 0;
}

static YtHttpBackend *httpOpen(const char *url, volatile int *cancelFlag)
{
   YtHttpBackend *b = (YtHttpBackend *)calloc(1, sizeof(*b));
   if (!b) return NULL;
   b->ring = (uint8_t *)malloc(HTTP_RING_SIZE);
   b->lock = SDL_CreateMutex();
   if (!b->ring || !b->lock) {
      if (b->lock) SDL_DestroyMutex(b->lock);
      free(b->ring);
      free(b);
      return NULL;
   }
   snprintf(b->url, sizeof(b->url), "%s", url);
   b->queryRange = strstr(url, "googlevideo.com") != NULL;
   b->size = queryNumber(url, "clen=");
   uint64_t dur = queryNumber(url, "dur=");
   if (b->size && dur) b->bytesPerSecond = b->size / dur;
   b->cancelFlag = cancelFlag;
   b->running = 1;
   b->thread = SDL_CreateThread(prefetchRun, "yt-source", b);
   if (!b->thread) {
      b->running = 0;
      SDL_DestroyMutex(b->lock);
      free(b->ring);
      free(b);
      return NULL;
   }
   return b;
}

static void httpClose(YtHttpBackend *b)
{
   b->running = 0;
   b->aborted = 1;
   if (b->thread) SDL_WaitThread(b->thread, NULL);   /* bounded: progress-cancel + 5 s low-speed abort */
   if (b->lock) SDL_DestroyMutex(b->lock);
   free(b->ring);
   free(b);
}

/* backend read: copy what's buffered at readPos, waiting (in cancel-
 * aware slices) for the prefetch thread when the ring is dry. */
static int64_t httpRead(YtHttpBackend *b, void *out, uint64_t length)
{
   int starvedMs = 0;
   for (;;) {
      if (httpCancelled(b) || b->aborted) return -1;
      SDL_LockMutex(b->lock);
      /* buffered bytes are served even after an error latches (a spent
       * address may already hold seconds of media); the error only
       * bites once the ring runs dry */
      if (b->readPos >= b->ringStart && b->readPos < b->ringEnd) {
         uint64_t available = b->ringEnd - b->readPos;
         uint64_t take = length < available ? length : available;
         uint8_t *dst = (uint8_t *)out;
         uint64_t done = 0;
         while (done < take) {
            size_t slot = (size_t)((b->readPos + done) % HTTP_RING_SIZE);
            size_t contiguous = HTTP_RING_SIZE - slot;
            size_t chunk = (size_t)(take - done) < contiguous ? (size_t)(take - done) : contiguous;
            memcpy(dst + done, b->ring + slot, chunk);
            done += chunk;
         }
         b->readPos += take;
         /* keep a small history behind the reader; drop the rest so
          * the prefetch has room to run ahead */
         if (b->readPos > b->ringStart + HTTP_HISTORY) b->ringStart = b->readPos - HTTP_HISTORY;
         SDL_UnlockMutex(b->lock);
         return (int64_t)take;
      }
      if (b->readPos < b->ringStart) {
         /* the reader fell behind the kept window: re-anchor the ring
          * at the read position (a refetch - counted by the server,
          * so the player avoids seeking in normal playback) */
         b->ringStart = b->ringEnd = b->readPos;
         b->generation++;
         b->atEof = 0;
         SDL_UnlockMutex(b->lock);
         continue;
      }
      if (b->error) { SDL_UnlockMutex(b->lock); return -1; }
      int eof = b->atEof;
      SDL_UnlockMutex(b->lock);
      if (eof) return 0;
      SDL_Delay(2);
      starvedMs += 2;
      if (starvedMs >= 3000) {
         yt_log_sink("source", "reader starved 3s waiting for the prefetch ring");
         starvedMs = 0;
      }
   }
}

static int httpSeek(YtHttpBackend *b, uint64_t offset)
{
   SDL_LockMutex(b->lock);
   b->readPos = offset;
   if (offset < b->ringStart || offset > b->ringEnd) {
      b->ringStart = b->ringEnd = offset;
      b->generation++;
      b->atEof = 0;
   }
   SDL_UnlockMutex(b->lock);
   return 0;
}

#endif /* NEON_PS3 */

/* ==================================================================== */
/* backend primitives                                                   */
/* ==================================================================== */

static int64_t readBackend(VideoSource *source, void *out, uint64_t length)
{
#ifdef NEON_PS3
   if (source->isHttp) return httpRead((YtHttpBackend *)source->http, out, length);
#else
   if (source->isHttp) return -1;
#endif
   return (int64_t)fread(out, 1, (size_t)length, source->file);
}

static int seekBackend(VideoSource *source, uint64_t offset)
{
#ifdef NEON_PS3
   if (source->isHttp) return httpSeek((YtHttpBackend *)source->http, offset);
#else
   if (source->isHttp) return -1;
#endif
   return fseek(source->file, (long)offset, SEEK_SET) == 0 ? 0 : -1;
}

/* ==================================================================== */
/* open / close                                                         */
/* ==================================================================== */

static int isHttpPath(const char *path)
{
   return strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0;
}

void videoSourceSetCancel(VideoSource *source, volatile int *flag)
{
   source->cancelFlag = flag;
}

int openVideoSource(VideoSource *source, const char *path)
{
   volatile int *cancelFlag = source->cancelFlag;   /* set before open; memset below would lose it */
   memset(source, 0, sizeof *source);
   source->cancelFlag = cancelFlag;

   source->buffer = (uint8_t *)malloc(SOURCE_BUFFER_SIZE);
   if (!source->buffer) return -1;

   if (isHttpPath(path)) {
#ifdef NEON_PS3
      YtHttpBackend *b = httpOpen(path, cancelFlag);
      if (!b) { free(source->buffer); source->buffer = 0; return -1; }
      source->http = b;
      source->isHttp = 1;
      source->size = b->size;
#else
      free(source->buffer);
      source->buffer = 0;
      return -1;   /* host builds have no network backend */
#endif
   } else {
      source->file = fopen(path, "rb");
      if (!source->file) { free(source->buffer); source->buffer = 0; return -1; }
      if (fseek(source->file, 0, SEEK_END) == 0) {
         long end = ftell(source->file);
         source->size = end > 0 ? (uint64_t)end : 0;
         fseek(source->file, 0, SEEK_SET);
      }
   }
   source->isOpen = 1;
   return 0;
}

/* ==================================================================== */
/* read-ahead layer (upstream's design, unchanged in shape)             */
/* ==================================================================== */

static int seekDescriptor(VideoSource *source, uint64_t offset)
{
   if (source->filePos == offset) return 0;
   if (seekBackend(source, offset) != 0) return -1;
   source->filePos = offset;
   return 0;
}

int64_t readVideoSource(VideoSource *source, void *buffer, uint64_t length)
{
   if (!source->isOpen) return -1;

   uint8_t *out = (uint8_t *)buffer;
   uint64_t served = 0;
   while (length > 0) {
      if (source->pos >= source->bufferStart && source->pos < source->bufferStart + source->bufferLen) {
         uint64_t available = source->bufferStart + source->bufferLen - source->pos;
         uint64_t take = length < available ? length : available;
         memcpy(out, source->buffer + (source->pos - source->bufferStart), (size_t)take);
         out += take; source->pos += take; length -= take; served += take;
         continue;
      }

      if (length >= SOURCE_BUFFER_SIZE) {
         if (seekDescriptor(source, source->pos) != 0) break;
         int64_t got = readBackend(source, out, length);
         if (got <= 0) break;
         out += got; source->pos += got; source->filePos += got; length -= got; served += got;
         continue;
      }

      if (seekDescriptor(source, source->pos) != 0) break;
      int64_t got = readBackend(source, source->buffer, SOURCE_BUFFER_SIZE);
      if (got <= 0) break;
      source->bufferStart = source->pos;
      source->bufferLen   = (int)got;
      source->filePos     = source->pos + got;
   }
   return (int64_t)served;
}

int seekVideoSource(VideoSource *source, uint64_t offset)
{
   if (!source->isOpen) return -1;
   source->pos = offset;   /* the backend is only moved lazily on the next buffer miss */
   return 0;
}

uint64_t getVideoSourcePosition(const VideoSource *source) { return source->pos; }

uint64_t getVideoSourceSize(const VideoSource *source)
{
#ifdef NEON_PS3
   /* the http backend may learn the size late (Content-Range) */
   if (source->isHttp && source->http) {
      YtHttpBackend *b = (YtHttpBackend *)source->http;
      SDL_LockMutex(b->lock);
      uint64_t size = b->size;
      SDL_UnlockMutex(b->lock);
      if (size) return size;
   }
#endif
   return source->size;
}

void cancelVideoSource(VideoSource *source)
{
#ifdef NEON_PS3
   if (source->isOpen && source->isHttp && source->http)
      ((YtHttpBackend *)source->http)->aborted = 1;
#else
   (void)source;
#endif
}

int videoSourceError(const VideoSource *source)
{
#ifdef NEON_PS3
   if (source->isHttp && source->http) {
      YtHttpBackend *b = (YtHttpBackend *)source->http;
      SDL_LockMutex(b->lock);
      int error = b->error;
      SDL_UnlockMutex(b->lock);
      return error;
   }
#else
   (void)source;
#endif
   return 0;
}

void closeVideoSource(VideoSource *source)
{
   if (source->isOpen) {
#ifdef NEON_PS3
      if (source->isHttp) httpClose((YtHttpBackend *)source->http);
      else if (source->file) fclose(source->file);
#else
      if (source->file) fclose(source->file);
#endif
      source->isOpen = 0;
      source->http = NULL;
      source->file = NULL;
   }
   free(source->buffer);
   source->buffer = 0;
}

int readVideoSourceAt(VideoSource *source, uint64_t offset, void *buffer, uint64_t length)
{
   if (seekVideoSource(source, offset) != 0) return -1;

   uint8_t *out = (uint8_t *)buffer;
   uint64_t remaining = length;
   while (remaining > 0) {
      int64_t got = readVideoSource(source, out, remaining);
      if (got <= 0) return -1;
      out       += got;
      remaining -= (uint64_t)got;
   }
   return 0;
}
