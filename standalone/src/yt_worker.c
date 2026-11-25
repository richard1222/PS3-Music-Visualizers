/* yt_worker.c - see yt_worker.h. One SDL thread does all YouTube network
 * work. Result structs are static and only touched by the worker while a
 * job runs; the UI reads them after the job's OK state lands, so no lock
 * is needed (the pc_audio hand-off pattern). Thumbnails are the one
 * exception (PLAYBACK2-DIAGNOSIS fix 4): decoded surfaces are staged in
 * a worker-owned pending array and adopted by the UI thread through
 * yt_worker_adopt_thumbs(), so a published surface is only ever freed
 * on the UI thread, between draws - never by the worker while the UI
 * may be blitting it. */
#include "yt_worker.h"
#include "yt_net.h"
#include <jpeglib.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define YT_THUMB_W 71
#define YT_THUMB_H 40
#define YT_THUMB_MAX 24
#define YT_THUMB_CAP (768 * 1024)

static SDL_Thread *worker;
static SDL_sem *wake;
static SDL_atomic_t quit_flag, job_state, job_kind_done, thumbs_abort;
static int job_kind;
static char job_query[160];
static char job_video[64];
static char job_error_text[160];
/* One-deep pending slot: a real job queued while thumbnails stream in
 * supersedes them (thumbnail fetches check thumbs_abort between files),
 * so picking a result never waits for decoration to finish. */
static int pending_kind;
static char pending_video[64];

static SearchResults results;
static StreamInfo stream_info;
/* Published thumbnails: written and freed ONLY on the UI thread
 * (adoption / clear), read by the UI's draw. Pending thumbnails:
 * filled by the worker, swapped into the published array by
 * yt_worker_adopt_thumbs(). Both arrays are only ever mutated
 * under thumbs_lock, so the swap itself can never race a free. */
static SDL_Surface *thumbs[YT_THUMB_MAX];
static SDL_Surface *thumbs_pending[YT_THUMB_MAX];
static SDL_mutex *thumbs_lock;
static char *thumb_buf;

static void thumbs_free_all(SDL_Surface **arr)
{
    for (int i = 0; i < YT_THUMB_MAX; i++) {
        if (arr[i]) SDL_FreeSurface(arr[i]);
        arr[i] = NULL;
    }
}

/* ---- JPEG thumbnails (libjpeg, decode in memory, scale once) ----------- */

typedef struct { struct jpeg_error_mgr pub; jmp_buf jb; } JpegErr;

static void jpg_exit(j_common_ptr cinfo)
{
    longjmp(((JpegErr *)cinfo->err)->jb, 1);
}

static SDL_Surface *decode_thumb(const unsigned char *data, size_t len)
{
    struct jpeg_decompress_struct cinfo;
    JpegErr err;
    cinfo.err = jpeg_std_error(&err.pub);
    err.pub.error_exit = jpg_exit;
    if (setjmp(err.jb)) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, (unsigned long)len);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    SDL_Surface *full = SDL_CreateRGBSurfaceWithFormat(0, (int)cinfo.output_width,
                                                       (int)cinfo.output_height, 24, SDL_PIXELFORMAT_RGB24);
    if (!full) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = (JSAMPROW)((Uint8 *)full->pixels + (size_t)cinfo.output_scanline * (size_t)full->pitch);
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    SDL_Surface *small = SDL_CreateRGBSurfaceWithFormat(0, YT_THUMB_W, YT_THUMB_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (small) SDL_BlitScaled(full, NULL, small, NULL);
    SDL_FreeSurface(full);
    return small;
}

static int fetch_thumbs(void)
{
    /* Stage into the pending array only (fix 4): the published
     * array belongs to the UI thread. Leftovers from an aborted
     * run are freed here, under the lock, so a UI-thread adoption
     * can never be swapping a pointer this free invalidates. */
    SDL_LockMutex(thumbs_lock);
    thumbs_free_all(thumbs_pending);
    SDL_UnlockMutex(thumbs_lock);
    if (!thumb_buf) thumb_buf = (char *)malloc(YT_THUMB_CAP);
    if (!thumb_buf) return -1;
    HttpHeader headers[] = {
        { "User-Agent", "Mozilla/5.0 (PS3; NeonLounge)" },
        { "Accept-Encoding", "identity" },
    };
    int n = results.count < YT_THUMB_MAX ? results.count : YT_THUMB_MAX;
    int got = 0;
    for (int i = 0; i < n; i++) {
        if (SDL_AtomicGet(&quit_flag)) return -1;
        if (SDL_AtomicGet(&thumbs_abort)) return -2;   /* superseded by a real job */
        char url[128];
        snprintf(url, sizeof(url), "https://i.ytimg.com/vi/%s/hqdefault.jpg", results.items[i].videoId);
        int len = 0, status = 0;
        int rc = fetchHttp("GET", url, headers, 2, NULL, 0, thumb_buf, YT_THUMB_CAP, &len, &status);
        if (rc == 0 && status == 200 && len > 0) {
            SDL_Surface *t = decode_thumb((const unsigned char *)thumb_buf, (size_t)len);
            if (t) {
                SDL_LockMutex(thumbs_lock);
                if (thumbs_pending[i]) SDL_FreeSurface(thumbs_pending[i]);
                thumbs_pending[i] = t;
                SDL_UnlockMutex(thumbs_lock);
                got++;
            }
        }
    }
    /* thumbnails are decoration: missing ones leave blank tiles, not an error */
    return (got > 0 || n == 0) ? 0 : -1;
}

/* ---- worker ------------------------------------------------------------- */

static int run(void *unused)
{
    (void)unused;
    while (!SDL_AtomicGet(&quit_flag)) {
        SDL_SemWait(wake);
        if (SDL_AtomicGet(&quit_flag)) break;
        for (;;) {
            int kind = job_kind;
            int rc = 0;
            yt_net_cancel(0);
            /* Published thumbnails are NOT cleared here any more
             * (fix 4): they are UI-thread property. The UI clears
             * them itself when new results land, and adopts the
             * worker's pending surfaces between draws. */
            switch (kind) {
                case YT_JOB_SEARCH:   rc = yt_search(job_query, &results); break;
                case YT_JOB_TRENDING: rc = yt_trending(TREND_GAMING, &results); break;
                case YT_JOB_EXTRACT:  rc = yt_extract(job_video, &stream_info); break;
                case YT_JOB_THUMBS:   rc = fetch_thumbs(); break;
                default:              rc = -1; break;
            }
            if (SDL_AtomicGet(&quit_flag)) return 0;
            if (pending_kind != YT_JOB_NONE) {
                /* a superseded job publishes nothing; run the pending one */
                job_kind = pending_kind;
                if (pending_kind == YT_JOB_EXTRACT)
                    snprintf(job_video, sizeof(job_video), "%s", pending_video);
                pending_kind = YT_JOB_NONE;
                SDL_AtomicSet(&thumbs_abort, 0);
                continue;
            }
            SDL_AtomicSet(&job_kind_done, kind);
            if (rc == 0) {
                SDL_AtomicSet(&job_state, YT_STATE_OK);
            } else {
                snprintf(job_error_text, sizeof(job_error_text),
                         "%s failed (code %d). Check the network connection.",
                         kind == YT_JOB_SEARCH ? "Search" :
                         kind == YT_JOB_TRENDING ? "Trending" :
                         kind == YT_JOB_EXTRACT ? "Resolve" : "Thumbnails", rc);
                SDL_AtomicSet(&job_state, YT_STATE_ERROR);
            }
            break;
        }
    }
    return 0;
}

int yt_worker_start(void)
{
    if (worker) return 0;
    if (!thumbs_lock) thumbs_lock = SDL_CreateMutex();
    if (!thumbs_lock) return -1;
    SDL_AtomicSet(&quit_flag, 0);
    SDL_AtomicSet(&job_state, YT_STATE_IDLE);
    SDL_AtomicSet(&job_kind_done, YT_JOB_NONE);
    wake = SDL_CreateSemaphore(0);
    if (!wake) return -1;
    worker = SDL_CreateThread(run, "YouTube net", NULL);
    if (!worker) {
        SDL_DestroySemaphore(wake);
        wake = NULL;
        return -1;
    }
    return 0;
}

void yt_worker_stop(void)
{
    yt_net_cancel(1);
    SDL_AtomicSet(&quit_flag, 1);
    if (wake) SDL_SemPost(wake);
    if (worker) SDL_WaitThread(worker, NULL);
    worker = NULL;
    if (wake) SDL_DestroySemaphore(wake);
    wake = NULL;
    /* The worker is joined above, so no lock is needed here. */
    thumbs_free_all(thumbs);
    thumbs_free_all(thumbs_pending);
    if (thumbs_lock) { SDL_DestroyMutex(thumbs_lock); thumbs_lock = NULL; }
    free(thumb_buf);
    thumb_buf = NULL;
    SDL_AtomicSet(&job_state, YT_STATE_IDLE);
}

static int queue_job(int kind)
{
    if (!worker) return -1;
    if (SDL_AtomicGet(&job_state) == YT_STATE_RUNNING) {
        /* only thumbnails may be superseded, and only once */
        if (job_kind != YT_JOB_THUMBS || kind == YT_JOB_THUMBS || pending_kind != YT_JOB_NONE)
            return -1;
        pending_kind = kind;
        if (kind == YT_JOB_EXTRACT)
            snprintf(pending_video, sizeof(pending_video), "%s", job_video);
        SDL_AtomicSet(&thumbs_abort, 1);
        yt_net_cancel(1);   /* cut the in-flight thumbnail fetch short too */
        return 0;
    }
    job_kind = kind;
    SDL_AtomicSet(&job_state, YT_STATE_RUNNING);
    SDL_SemPost(wake);
    return 0;
}

int yt_job_search(const char *query)
{
    if (!query || !*query) return -1;
    snprintf(job_query, sizeof(job_query), "%s", query);
    return queue_job(YT_JOB_SEARCH);
}

int yt_job_trending(void)
{
    return queue_job(YT_JOB_TRENDING);
}

int yt_job_extract(const char *videoId)
{
    if (!videoId || !*videoId) return -1;
    snprintf(job_video, sizeof(job_video), "%s", videoId);
    return queue_job(YT_JOB_EXTRACT);
}

int yt_job_thumbs(void)
{
    return queue_job(YT_JOB_THUMBS);
}

int yt_worker_poll(int *kind_out)
{
    int state = SDL_AtomicGet(&job_state);
    if ((state == YT_STATE_OK || state == YT_STATE_ERROR) && kind_out)
        *kind_out = SDL_AtomicGet(&job_kind_done);
    return state;
}

void yt_worker_consume(void)
{
    int state = SDL_AtomicGet(&job_state);
    if (state == YT_STATE_OK || state == YT_STATE_ERROR)
        SDL_AtomicSet(&job_state, YT_STATE_IDLE);
}

const SearchResults *yt_worker_results(void) { return &results; }
const StreamInfo *yt_worker_stream(void) { return &stream_info; }
const char *yt_worker_error(void) { return job_error_text; }

SDL_Surface *yt_worker_thumb(int index)
{
    if (index < 0 || index >= YT_THUMB_MAX) return NULL;
    return thumbs[index];
}

/* UI thread, once per frame (or at least between draws): move
 * every staged thumbnail into the published array, freeing the
 * surface it replaces. This is the only place pending surfaces
 * become visible, and because it runs on the UI thread between
 * draws, the surface being freed is never one the UI is in the
 * middle of blitting (the 1.13 use-after-free window at job
 * boundaries). Progressive pop-in is preserved: each decoded
 * thumbnail is adopted on the next frame after it lands. */
void yt_worker_adopt_thumbs(void)
{
    if (!thumbs_lock) return;
    SDL_LockMutex(thumbs_lock);
    for (int i = 0; i < YT_THUMB_MAX; i++) {
        if (thumbs_pending[i]) {
            if (thumbs[i]) SDL_FreeSurface(thumbs[i]);
            thumbs[i] = thumbs_pending[i];
            thumbs_pending[i] = NULL;
        }
    }
    SDL_UnlockMutex(thumbs_lock);
}

/* UI thread: drop all published thumbnails (a new results set is
 * landing). The worker's pending array is untouched; anything it
 * stages later is adopted over the cleared slots. */
void yt_worker_thumbs_clear(void)
{
    if (!thumbs_lock) return;
    SDL_LockMutex(thumbs_lock);
    thumbs_free_all(thumbs);
    SDL_UnlockMutex(thumbs_lock);
}
