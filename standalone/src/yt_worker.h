/* yt_worker.h - the YouTube network worker.
 *
 * One SDL thread (the pc_audio pattern: a cancel atomic, a state atomic,
 * mutex-free result hand-off through job completion) runs every blocking
 * extractor call and thumbnail fetch, so the UI thread never touches the
 * network. Jobs run one at a time; starting a job while another runs is
 * refused. yt_worker_stop() cancels any in-flight fetch (yt_net_cancel)
 * and joins the thread - the join is bounded in practice because every
 * transfer aborts on cancel and carries its own timeouts. */
#ifndef NL_YT_WORKER_H
#define NL_YT_WORKER_H

#include <SDL.h>
#include "youtube.h"

enum { YT_JOB_NONE = 0, YT_JOB_SEARCH, YT_JOB_TRENDING, YT_JOB_EXTRACT, YT_JOB_THUMBS };
/* yt_worker_poll() states */
enum { YT_STATE_IDLE = 0, YT_STATE_RUNNING = 1, YT_STATE_OK = 2, YT_STATE_ERROR = -1 };

int  yt_worker_start(void);
void yt_worker_stop(void);

/* Queue a job. 0 ok, -1 if the worker is busy or not started. */
int yt_job_search(const char *query);
int yt_job_trending(void);
int yt_job_extract(const char *videoId);
/* Fetch + decode thumbnails for the current results (call after a
 * search/trending job completes). */
int yt_job_thumbs(void);

/* Poll from the UI thread. Returns a YT_STATE_*; when OK or ERROR,
 * *kind_out receives the finished YT_JOB_*. Results stay readable until
 * yt_worker_consume(). */
int yt_worker_poll(int *kind_out);
void yt_worker_consume(void);

/* Valid after a SEARCH/TRENDING job finished OK. */
const SearchResults *yt_worker_results(void);
/* Valid after an EXTRACT job finished OK. */
const StreamInfo *yt_worker_stream(void);
/* Short human-readable failure for the last ERROR job. */
const char *yt_worker_error(void);

/* Decoded, pre-scaled thumbnail surface for results index i, or NULL.
 * Owned by the UI thread: published surfaces are swapped in and
 * freed only by yt_worker_adopt_thumbs() / yt_worker_thumbs_clear()
 * on that thread, so a returned surface stays valid while the UI
 * draws with it. */
SDL_Surface *yt_worker_thumb(int index);

/* UI thread: adopt every thumbnail the worker has staged since
 * the last call (freeing the published surface each replaces).
 * Call once per frame, between draws. */
void yt_worker_adopt_thumbs(void);
/* UI thread: drop all published thumbnails (new results landing). */
void yt_worker_thumbs_clear(void);

#endif
