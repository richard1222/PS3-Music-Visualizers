/* yt_player.c - YouTube playback engine. See yt_player.h for the
 * shape and the freeze-rule notes. PS3-only (threads, vdec, FAAD2);
 * the host build never links this file. */
#include "yt_player.h"
#include "yt_pick.h"
#include "yt_sync.h"
#include "yt_demux.h"
#include "yt_aac.h"
#include "ps3vdec.h"
#include "h264.h"
#include "demux-mp4.h"
#include "video-source.h"
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Feed pacing (PLAYBACK2-DIAGNOSIS fix 1): the ahead window is a
 * few frame intervals (yt_sync_feed_ahead_ns, ~120-200 ms), and
 * submissions also gate on display-ring pressure - never let
 * RING_READY_MAX or more published frames sit unconsumed. The
 * 1.13 build fed 1.5 s ahead into a 4-slot ring whose overflow
 * policy trashed the excess at publish time: 566 frames decoded,
 * 51 presented (~2.7 fps bursts). Paced to presentation, the
 * ring holds the next DUE frames and nothing is trashed.
 * PLAYBACK3 fix 4: the gate is a fixed 6, no longer derived from
 * the ring size - the ring grew to 12 slots, and 6 ready + the
 * decoder's 3 in flight + 1 displaying/frozen = 10 < 12 makes
 * PICOUT overflow structurally impossible (the 1.14 derivation
 * PIC_RING-2 overshot: 6 + 3 in flight = 9 > 8 slots, and the
 * ring retired ~17 frames per network burst). */
#define RING_READY_MAX 6
#define PREROLL_US    5000000ull      /* audio preroll timeout (mohasi: 5 s) */
#define MAX_REEXTRACT 3               /* per stream, per session (PLAN.md) */
#define PCM_SECONDS   3               /* PCM ring depth */

static void plog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    yt_log_sink("player", buf);
}

static uint64_t now_us(void)
{
    static uint64_t base = 0, freq = 0;
    uint64_t c = SDL_GetPerformanceCounter();
    if (!freq) { freq = SDL_GetPerformanceFrequency(); base = c; return 0; }
    return (c - base) * 1000000ull / freq;
}

typedef struct {
    /* written by yt_player_start (main thread), then read-only */
    StreamInfo info;
    YtPick     pick;
    char       video_id[64];
    char       title[256];
    int        audio_split;

    SDL_atomic_t state;
    SDL_atomic_t cancel;
    volatile int src_cancel;        /* the video sources watch this */
    SDL_atomic_t paused_flag;
    SDL_atomic_t feed_done, audio_done, video_ready, output_open;
    char         error_text[160];

    SDL_Thread *feed_thread, *audio_thread;
    int         zombie;             /* a stop timed out; workers still finishing */

    Mp4Demuxer vdemux; int vdemux_open;
    Mp4Demuxer ademux; int ademux_open;
    int        vdec_open;
    int        coded_w, coded_h;

    YtWallClock wall;
    uint64_t    start_us;
    volatile int      anchor_valid;
    volatile uint64_t anchor_pts_ns;
    volatile uint64_t played_frames;
    volatile int      audio_format_known, audio_rate, audio_channels;
    volatile int      audio_absent, audio_eos, video_eos;

    SDL_mutex *pcm_lock;
    int16_t   *pcm_buf;
    int        pcm_cap;             /* frames */
    int        pcm_ch;
    uint64_t   pcm_head, pcm_tail;  /* monotonic frame counters, under pcm_lock */

    YtAac *aac;

    uint64_t duration_ns;
    uint64_t last_present_pts_ns;
    int      frames_presented;
    uint64_t end_since_us;
    unsigned sync_drops, underruns, submits, submit_drops;
    int      reextracts_v, reextracts_a;
    SDL_Surface *frame_surface;
    int      frame_surf_w, frame_surf_h;
    /* Frame persistence (PLAYBACK2 fix 2, PLAYBACK3 fix 2): the
     * frozen frame is the presented frame's own ring slot, held
     * by ps3vdec (ps3vdec_display_hold_frozen) - no copy. This
     * surface only WRAPS that slot's pixels for the repaint; the
     * 1.14 build instead copied the whole frame into an owned
     * surface every present (~2.8 MB of PPU memcpy per frame),
     * a material part of the presenter overrunning its budget. */
    SDL_Surface *frozen_surface;
    int      frozen_surf_w, frozen_surf_h;
    int      last_dw, last_dh;      /* visible size of the frozen frame */
    int      last_valid;
    /* 1 Hz stats line bookkeeping (PLAYBACK3 fix 0). */
    uint64_t stat_last_us;
    int      stat_last_presented;
    uint32_t stat_last_skipped;
} YtPlayer;

static YtPlayer P;
static SDL_mutex *g_reextract_lock = NULL;
static int g_vdec_global_done = 0;

static void set_error(const char *text)
{
    snprintf(P.error_text, sizeof(P.error_text), "%s", text);
    plog("[yt-player] error: %s", text);
}

/* The presentation clock: audio-master once FAAD2 has produced
 * sound, the pause-aware wall clock otherwise. UINT64_MAX = no
 * clock yet (nothing submitted, nothing decoded). */
static uint64_t clock_ns(void)
{
    if (P.anchor_valid)
        return yt_sync_audio_clock_ns(P.anchor_pts_ns, P.played_frames, P.audio_rate);
    if (P.wall.anchored)
        return yt_wall_now(&P.wall, now_us());
    return UINT64_MAX;
}

static int pcm_available_frames(void)
{
    SDL_LockMutex(P.pcm_lock);
    uint64_t n = P.pcm_head - P.pcm_tail;
    SDL_UnlockMutex(P.pcm_lock);
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* PCM sink: runs on the audio thread inside yt_aac_feed. Pushes     */
/* decoded PCM into the ring the SDL callback drains; waiting for    */
/* ring room here is what paces the audio thread to real time.       */
/* ------------------------------------------------------------------ */
static void pcm_cb(void *ctx, const int16_t *samples, int frames, int channels, uint64_t pts_ns)
{
    (void)ctx;
    int want_ch = channels >= 2 ? 2 : 1;
    int rate = yt_aac_rate(P.aac);
    if (rate <= 0) return;
    if (P.pcm_buf && (P.pcm_ch != want_ch || P.audio_rate != rate)) {
        free(P.pcm_buf);            /* format changed between videos */
        P.pcm_buf = NULL;
    }
    if (!P.pcm_buf) {
        P.pcm_ch = want_ch;
        P.pcm_cap = rate * PCM_SECONDS;
        P.pcm_buf = (int16_t *)malloc(sizeof(int16_t) * (size_t)P.pcm_cap * (size_t)want_ch);
        if (!P.pcm_buf) { P.audio_absent = 1; return; }
        SDL_LockMutex(P.pcm_lock);
        P.pcm_head = P.pcm_tail = 0;
        SDL_UnlockMutex(P.pcm_lock);
        P.audio_rate = rate;
        P.audio_channels = want_ch;
        P.audio_format_known = 1;
        plog("[yt-player] audio format: %d Hz, %d channel(s)", rate, want_ch);
    }
    if (!P.anchor_valid) {
        P.anchor_pts_ns = pts_ns;
        P.anchor_valid = 1;
        plog("[yt-player] audio clock anchored at %llu ms",
             (unsigned long long)(pts_ns / 1000000ull));
    }
    int written = 0;
    while (written < frames) {
        if (SDL_AtomicGet(&P.cancel)) return;
        SDL_LockMutex(P.pcm_lock);
        int room = P.pcm_cap - (int)(P.pcm_head - P.pcm_tail);
        int n = frames - written;
        if (n > room) n = room;
        int i;
        for (i = 0; i < n; i++) {
            int16_t *dst = P.pcm_buf + (size_t)((P.pcm_head + (uint64_t)i) % (uint64_t)P.pcm_cap) * (size_t)P.pcm_ch;
            const int16_t *src = samples + (size_t)(written + i) * (size_t)channels;
            dst[0] = src[0];
            if (P.pcm_ch == 2) dst[1] = channels >= 2 ? src[1] : src[0];
        }
        P.pcm_head += (uint64_t)n;
        written += n;
        SDL_UnlockMutex(P.pcm_lock);
        if (n == 0) SDL_Delay(5);
    }
}

int yt_player_audio_read(int16_t *dst, int samples)
{
    if (!P.pcm_buf || !SDL_AtomicGet(&P.output_open)) return 0;
    int want = samples / P.pcm_ch;
    if (want <= 0) return 0;
    SDL_LockMutex(P.pcm_lock);
    uint64_t avail = P.pcm_head - P.pcm_tail;
    int n = want;
    if ((uint64_t)n > avail) n = (int)avail;
    int i;
    for (i = 0; i < n; i++) {
        const int16_t *src = P.pcm_buf + (size_t)((P.pcm_tail + (uint64_t)i) % (uint64_t)P.pcm_cap) * (size_t)P.pcm_ch;
        int16_t *d = dst + (size_t)i * (size_t)P.pcm_ch;
        d[0] = src[0];
        if (P.pcm_ch == 2) d[1] = src[1];
    }
    P.pcm_tail += (uint64_t)n;
    SDL_UnlockMutex(P.pcm_lock);
    P.played_frames += (uint64_t)n;
    if (n < want) P.underruns++;
    return n * P.pcm_ch;
}

/* ------------------------------------------------------------------ */
/* Re-extract recovery (PLAN.md section 2): a spent googlevideo      */
/* address is replaced with a fresh one for the same itag and the    */
/* demuxer's source swapped at its parse position.                   */
/* ------------------------------------------------------------------ */
static int reextract_locked(Mp4Demuxer *d, int want_itag, int *counter, const char *which)
{
    if (*counter >= MAX_REEXTRACT) return -1;
    if (SDL_AtomicGet(&P.cancel)) return -1;
    StreamInfo fresh;
    memset(&fresh, 0, sizeof(fresh));
    if (yt_extract(P.video_id, &fresh) != 0) {
        plog("[yt-player] re-extract failed (%s stream)", which);
        return -1;
    }
    const char *url = NULL;
    int i;
    for (i = 0; i < fresh.formatCount; i++) {
        if (fresh.formats[i].itag == want_itag && fresh.formats[i].url[0]) {
            url = fresh.formats[i].url;
            break;
        }
    }
    if (!url) {
        YtPick p2;
        if (yt_pick_formats(&fresh, &p2) == 0) {
            const StreamFormat *f = (which[0] == 'a') ? p2.audio : p2.video;
            if (f) url = f->url;
        }
    }
    if (!url) {
        plog("[yt-player] re-extract found no %s stream", which);
        return -1;
    }
    uint64_t pos = getVideoSourcePosition(&d->source);
    VideoSource ns;
    memset(&ns, 0, sizeof(ns));
    videoSourceSetCancel(&ns, &P.src_cancel);
    if (openVideoSource(&ns, url) != 0) return -1;
    seekVideoSource(&ns, pos);
    closeVideoSource(&d->source);
    d->source = ns;
    (*counter)++;
    plog("[yt-player] %s stream re-extracted (#%d), resuming at byte %llu",
         which, *counter, (unsigned long long)pos);
    return 0;
}

static int reextract(Mp4Demuxer *d, int want_itag, int *counter, const char *which)
{
    SDL_LockMutex(g_reextract_lock);
    int rc = reextract_locked(d, want_itag, counter, which);
    SDL_UnlockMutex(g_reextract_lock);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Video feed thread: owns the video demuxer and the decoder.        */
/* ------------------------------------------------------------------ */
static int feed_main(void *unused)
{
    (void)unused;
    int rc = yt_demux_open_video(&P.vdemux, P.pick.video->url, &P.src_cancel);
    if (rc != 0) {
        if (!SDL_AtomicGet(&P.cancel)) {
            set_error("Could not open the video stream");
            SDL_AtomicSet(&P.state, YT_PLAYER_ERROR);
        }
        goto out;
    }
    P.vdemux_open = 1;
    P.duration_ns = P.vdemux.durationNs
        ? P.vdemux.durationNs
        : (uint64_t)(P.info.durationSeconds > 0 ? P.info.durationSeconds : 0) * 1000000000ull;

    /* Cap the SPS reference-frame count to the decoder's DPB
     * budget (patchSpsToDpbCap, from the same mohasi tree): a
     * stream declaring more reference frames than level 4.2 holds
     * decodes to concealed black pictures. The patch edits the
     * demuxer's Annex-B header, which buildVideoAu prepends to
     * every keyframe AU, so one patch covers the whole stream. */
    {
        uint8_t *hdr = P.vdemux.h264.header;
        int hlen = P.vdemux.h264.headerSize;
        if (hlen > 4 && hdr[0] == 0 && hdr[1] == 0 && hdr[2] == 0 && hdr[3] == 1) {
            int sps_len = hlen - 4;
            int i;
            for (i = 4; i + 3 < hlen; i++) {
                if (hdr[i] == 0 && hdr[i + 1] == 0 && hdr[i + 2] == 0 && hdr[i + 3] == 1) {
                    sps_len = i - 4;
                    break;
                }
            }
            int pr = patchSpsToDpbCap(hdr + 4, sps_len);
            if (pr > 0) plog("[yt-player] SPS reference frames capped to %d", pr);
            else if (pr < 0) plog("[yt-player] SPS ref frames exceed the DPB and cannot be patched");
        }
    }

    /* The decoder opens at the SPS coded size: encoders pad (720
     * lines coded as 736) and a wrong size is a black screen. */
    int coded_w = 0, coded_h = 0;
    {
        H264StreamInfo si;
        if (readH264StreamInfo(P.vdemux.h264.header, P.vdemux.h264.headerSize, &si) == 0
            && si.codedWidth > 0 && si.codedHeight > 0) {
            coded_w = si.codedWidth;
            coded_h = si.codedHeight;
        }
    }
    if (!coded_w) {
        coded_w = (P.vdemux.width + 15) & ~15;
        coded_h = (P.vdemux.height + 15) & ~15;
    }
    P.coded_w = coded_w;
    P.coded_h = coded_h;

    if (!g_vdec_global_done) {
        if (ps3vdec_global_init() != 0) {
            if (!SDL_AtomicGet(&P.cancel)) {
                set_error("Video decoder failed to start");
                SDL_AtomicSet(&P.state, YT_PLAYER_ERROR);
            }
            goto out;
        }
        g_vdec_global_done = 1;
    }
    {
        int fps = P.pick.video->fps > 0 ? P.pick.video->fps : 30;
        if (ps3vdec_open(coded_w, coded_h, fps) != 0) {
            if (!SDL_AtomicGet(&P.cancel)) {
                set_error("Video decoder failed to open");
                SDL_AtomicSet(&P.state, YT_PLAYER_ERROR);
            }
            goto out;
        }
    }
    P.vdec_open = 1;
    plog("[yt-player] playing itag %d video (%dx%d coded) + itag %d audio%s",
         P.pick.video->itag, coded_w, coded_h,
         P.pick.audio ? P.pick.audio->itag : 0, P.pick.muxed ? " (muxed)" : "");
    SDL_AtomicSet(&P.video_ready, 1);
    SDL_AtomicSet(&P.state, YT_PLAYER_PLAYING);

    const uint64_t ahead_ns = yt_sync_feed_ahead_ns(P.pick.video->fps);
    plog("[yt-player] feed pacing: ahead window %llu ms, ring gate at %d ready frames",
         (unsigned long long)(ahead_ns / 1000000ull), RING_READY_MAX);
    {
        int have = 0, skip_to_key = 0;
        uint32_t frame_no = 0;
        VideoAu au;
        while (!SDL_AtomicGet(&P.cancel)) {
            if (SDL_AtomicGet(&P.paused_flag)) { SDL_Delay(10); continue; }
            if (!have) {
                rc = readMp4VideoAu(&P.vdemux, &au);   /* 1 = AU, 0 = end, -1 = error */
                if (rc == 0) { P.video_eos = 1; break; }
                if (rc < 0) {
                    int e = videoSourceError(&P.vdemux.source);
                    if (e == 1 && reextract(&P.vdemux, P.pick.video->itag,
                                            &P.reextracts_v, "video") == 0)
                        continue;
                    if (!SDL_AtomicGet(&P.cancel)) {
                        set_error(e == 1 ? "The video stream address expired"
                                         : "The video stream failed");
                        SDL_AtomicSet(&P.state, YT_PLAYER_ERROR);
                    }
                    break;
                }
                have = 1;
            }
            if (skip_to_key && !au.keyframe) { have = 0; continue; }
            /* Ring pressure gate: if the display ring is already
             * holding nearly a full set of unconsumed frames, the
             * decoder is ahead of presentation - wait here (the
             * AU stays in `have`) rather than submit pictures the
             * ring would only retire or trash. Cancel is checked
             * at the loop head, so this wait stays bounded. */
            if (ps3vdec_display_ready_count() >= RING_READY_MAX) { SDL_Delay(5); continue; }
            uint64_t clk = clock_ns();
            if (clk != UINT64_MAX) {
                if (au.pts > clk + ahead_ns) { SDL_Delay(5); continue; }
            } else {
                ps3vdec_stats_t st;
                ps3vdec_get_stats(&st);
                if (st.submitted - st.audone > 6) { SDL_Delay(5); continue; }
            }
            if (!P.wall.anchored) yt_wall_anchor(&P.wall, au.pts, now_us());
            if (ps3vdec_submit(au.data, (int)au.size, au.pts / 1000, frame_no) == 0) {
                frame_no++;
                P.submits++;
                skip_to_key = 0;
            } else {
                P.submit_drops++;
                skip_to_key = 1;
            }
            have = 0;
        }
    }
out:
    if (P.vdec_open) { ps3vdec_stop(); P.vdec_open = 0; }
    if (P.vdemux_open) { closeMp4Demuxer(&P.vdemux); P.vdemux_open = 0; }
    SDL_AtomicSet(&P.video_ready, 1);   /* release the audio thread either way */
    SDL_AtomicSet(&P.feed_done, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Audio thread: owns the audio demuxer (split mode) and FAAD2.      */
/* In muxed mode it drains the video demuxer's audio queue, which    */
/* the feed thread fills as it reads (upstream's design).            */
/* ------------------------------------------------------------------ */
static int audio_main(void *unused)
{
    (void)unused;
    while (!SDL_AtomicGet(&P.cancel) && !SDL_AtomicGet(&P.video_ready))
        SDL_Delay(5);
    if (SDL_AtomicGet(&P.cancel)) goto out;
    if (!P.vdemux_open) goto out;                    /* video failed */
    if (P.audio_split) {
        if (yt_demux_open_audio(&P.ademux, P.pick.audio->url, &P.src_cancel) != 0) {
            plog("[yt-player] audio stream open failed - playing silent");
            P.audio_absent = 1;
            goto out;
        }
        P.ademux_open = 1;
    } else if (!P.vdemux.hasAudio) {
        P.audio_absent = 1;
        goto out;
    }
    P.aac = yt_aac_create();
    if (!P.aac) { P.audio_absent = 1; goto out; }
    {
        Mp4Demuxer *ad = P.audio_split ? &P.ademux : &P.vdemux;
        int rate = ad->audioAdtsRate;
        int ch = ad->audioChannels;
        AudioAu au;
        while (!SDL_AtomicGet(&P.cancel)) {
            int rc;
            if (P.audio_split) rc = readMp4AudioAu(&P.ademux, &au);   /* 1 / 0 / -1 */
            else rc = takeQueuedAudioAu(&P.vdemux.audioQueue, &au);  /* 1 / 0 */
            if (rc != 1) {
                if (P.audio_split) {
                    if (rc == 0) { P.audio_eos = 1; break; }
                    int e = videoSourceError(&P.ademux.source);
                    if (e == 1 && reextract(&P.ademux, P.pick.audio->itag,
                                            &P.reextracts_a, "audio") == 0)
                        continue;
                    plog("[yt-player] audio stream failed - going silent");
                    P.audio_absent = 1;
                    break;
                }
                if (P.video_eos) { P.audio_eos = 1; break; }
                SDL_Delay(4);
                continue;
            }
            if (yt_aac_feed(P.aac, au.data, (int)au.size, au.pts, rate, ch,
                            pcm_cb, NULL) != 0) {
                plog("[yt-player] AAC decode failed - going silent");
                P.audio_absent = 1;
                break;
            }
        }
    }
out:
    if (P.ademux_open) { closeMp4Demuxer(&P.ademux); P.ademux_open = 0; }
    if (P.aac) { yt_aac_destroy(P.aac); P.aac = NULL; }
    SDL_AtomicSet(&P.audio_done, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main-thread API                                                   */
/* ------------------------------------------------------------------ */
int yt_player_start(const StreamInfo *info, const char *video_id)
{
    int st = SDL_AtomicGet(&P.state);
    if (st == YT_PLAYER_STARTING || st == YT_PLAYER_PLAYING) return -1;
    if (P.zombie) {
        if (SDL_AtomicGet(&P.feed_done) && SDL_AtomicGet(&P.audio_done)) {
            if (P.feed_thread) { SDL_WaitThread(P.feed_thread, NULL); P.feed_thread = NULL; }
            if (P.audio_thread) { SDL_WaitThread(P.audio_thread, NULL); P.audio_thread = NULL; }
            if (P.pcm_buf) { free(P.pcm_buf); P.pcm_buf = NULL; }
            if (P.frame_surface) { SDL_FreeSurface(P.frame_surface); P.frame_surface = NULL; }
            ps3vdec_display_release_frozen();
            if (P.frozen_surface) { SDL_FreeSurface(P.frozen_surface); P.frozen_surface = NULL; }
            P.last_valid = 0;
            P.zombie = 0;
        } else {
            set_error("The previous video is still closing");
            SDL_AtomicSet(&P.state, YT_PLAYER_ERROR);
            return -1;
        }
    }

    P.info = *info;
    snprintf(P.video_id, sizeof(P.video_id), "%s", video_id ? video_id : "");
    snprintf(P.title, sizeof(P.title), "%s", info->title);
    if (info->isLive) {
        set_error("Live streams are not supported yet");
        SDL_AtomicSet(&P.state, YT_PLAYER_ERROR);
        return -1;
    }
    if (yt_pick_formats(info, &P.pick) != 0) {
        set_error("No playable stream (needs H.264 + AAC)");
        SDL_AtomicSet(&P.state, YT_PLAYER_ERROR);
        return -1;
    }
    P.audio_split = (P.pick.audio && !P.pick.muxed) ? 1 : 0;

    if (!P.pcm_lock) P.pcm_lock = SDL_CreateMutex();
    if (!g_reextract_lock) g_reextract_lock = SDL_CreateMutex();

    /* reset the session */
    SDL_AtomicSet(&P.cancel, 0);
    P.src_cancel = 0;
    SDL_AtomicSet(&P.paused_flag, 0);
    SDL_AtomicSet(&P.feed_done, 0);
    SDL_AtomicSet(&P.audio_done, 0);
    SDL_AtomicSet(&P.video_ready, 0);
    SDL_AtomicSet(&P.output_open, 0);
    P.error_text[0] = 0;
    memset(&P.wall, 0, sizeof(P.wall));
    P.anchor_valid = 0;
    P.anchor_pts_ns = 0;
    P.played_frames = 0;
    P.audio_format_known = 0;
    P.audio_rate = 0;
    P.audio_channels = 0;
    P.audio_absent = 0;
    P.audio_eos = 0;
    P.video_eos = 0;
    P.vdemux_open = 0;
    P.ademux_open = 0;
    P.vdec_open = 0;
    SDL_LockMutex(P.pcm_lock);
    P.pcm_head = P.pcm_tail = 0;
    SDL_UnlockMutex(P.pcm_lock);
    P.duration_ns = (uint64_t)(info->durationSeconds > 0 ? info->durationSeconds : 0) * 1000000000ull;
    P.last_present_pts_ns = 0;
    P.frames_presented = 0;
    P.end_since_us = 0;
    P.sync_drops = 0;
    P.underruns = 0;
    P.submits = 0;
    P.submit_drops = 0;
    P.reextracts_v = 0;
    P.reextracts_a = 0;
    P.last_valid = 0;                     /* no frame from a previous video may show */
    P.stat_last_us = 0;
    P.stat_last_presented = 0;
    P.stat_last_skipped = 0;
    (void)now_us();                       /* warm the clock base here */
    P.start_us = now_us();

    SDL_AtomicSet(&P.state, YT_PLAYER_STARTING);
    P.feed_thread = SDL_CreateThread(feed_main, "yt-play-feed", NULL);
    P.audio_thread = SDL_CreateThread(audio_main, "yt-play-audio", NULL);
    if (!P.feed_thread || !P.audio_thread) {
        SDL_AtomicSet(&P.cancel, 1);
        P.src_cancel = 1;
        set_error("Could not start playback threads");
        SDL_AtomicSet(&P.state, YT_PLAYER_ERROR);
        return -1;
    }
    plog("[yt-player] start '%s' (%s)", P.title, P.video_id);
    return 0;
}

void yt_player_stop(void)
{
    if (SDL_AtomicGet(&P.state) == YT_PLAYER_IDLE) return;
    SDL_AtomicSet(&P.cancel, 1);
    P.src_cancel = 1;
    /* Every loop the workers run is cancel-aware and every wait
     * bounded, so this deadline is generous; if it ever expires the
     * workers still finish on their own and own their resources
     * until they do (the zombie path in yt_player_start). */
    uint64_t deadline = now_us() + 4000000ull;
    int done = 0;
    while (now_us() < deadline) {
        if (SDL_AtomicGet(&P.feed_done) && SDL_AtomicGet(&P.audio_done)) { done = 1; break; }
        SDL_Delay(5);
    }
    if (done) {
        if (P.feed_thread) { SDL_WaitThread(P.feed_thread, NULL); P.feed_thread = NULL; }
        if (P.audio_thread) { SDL_WaitThread(P.audio_thread, NULL); P.audio_thread = NULL; }
        if (P.pcm_buf) { free(P.pcm_buf); P.pcm_buf = NULL; }
        if (P.frame_surface) { SDL_FreeSurface(P.frame_surface); P.frame_surface = NULL; }
        ps3vdec_display_release_frozen();
        if (P.frozen_surface) { SDL_FreeSurface(P.frozen_surface); P.frozen_surface = NULL; }
        P.last_valid = 0;
        ps3vdec_stats_t vst;
        ps3vdec_get_stats(&vst);
        plog("[yt-player] stopped: presented=%d sync_drops=%u underruns=%u submits=%u submit_drops=%u disp_dropped=%u disp_skipped=%u reextract v/a=%d/%d",
             P.frames_presented, P.sync_drops, P.underruns, P.submits,
             P.submit_drops, vst.disp_dropped, vst.disp_skipped,
             P.reextracts_v, P.reextracts_a);
    } else {
        if (P.feed_thread) { SDL_DetachThread(P.feed_thread); P.feed_thread = NULL; }
        if (P.audio_thread) { SDL_DetachThread(P.audio_thread); P.audio_thread = NULL; }
        P.zombie = 1;
        plog("[yt-player] stop timed out - workers finishing in the background");
    }
    SDL_AtomicSet(&P.output_open, 0);
    SDL_AtomicSet(&P.state, YT_PLAYER_IDLE);
}

int yt_player_state(void) { return SDL_AtomicGet(&P.state); }
const char *yt_player_error(void) { return P.error_text; }
const char *yt_player_title(void) { return P.title; }

int yt_player_audio_format(int *rate, int *channels)
{
    if (P.audio_absent) return -1;
    if (!P.audio_format_known) return 0;
    if (rate) *rate = P.audio_rate;
    if (channels) *channels = P.audio_channels;
    return 1;
}

void yt_player_set_output_open(int on) { SDL_AtomicSet(&P.output_open, on ? 1 : 0); }

void yt_player_set_paused(int paused)
{
    SDL_AtomicSet(&P.paused_flag, paused ? 1 : 0);
    yt_wall_set_paused(&P.wall, paused, now_us());
}

int yt_player_paused(void) { return SDL_AtomicGet(&P.paused_flag); }

uint64_t yt_player_position_ns(void)
{
    uint64_t c = clock_ns();
    return c != UINT64_MAX ? c : P.last_present_pts_ns;
}

uint64_t yt_player_duration_ns(void) { return P.duration_ns; }
int yt_player_buffering(void) { return P.frames_presented == 0; }

/* Letterboxed blit of a coded-size surface's visible (dw x dh)
 * picture into the canvas. Shared by live presentation and by
 * the frozen-frame repaint below. PLAYBACK3 fix 3: when the
 * letterbox result is exactly the picture size the blit is 1:1
 * and takes SDL_BlitSurface's plain copy instead of the generic
 * software scaler (the common native-size case: 960x720 into the
 * 1280x720 canvas scales by exactly 1.0). */
static void blit_letterboxed(SDL_Surface *canvas, SDL_Surface *surf, int dw, int dh)
{
    SDL_Rect src;
    src.x = 0; src.y = 0; src.w = dw; src.h = dh;
    SDL_Rect dst;
    yt_sync_letterbox(canvas->w, canvas->h, dw, dh,
                      &dst.x, &dst.y, &dst.w, &dst.h);
    if (dst.w == dw && dst.h == dh)
        SDL_BlitSurface(surf, &src, canvas, &dst);
    else
        SDL_BlitScaled(surf, &src, canvas, &dst);
}

/* Wrap a decoder-owned pixel buffer (coded size, ARGB32) in the
 * given surface slot, (re)creating the surface when the coded
 * size changed. The surface never owns the pixels; the pointer
 * is re-aimed on every call. Returns the surface or NULL. */
static SDL_Surface *wrap_pixels(SDL_Surface **slot, int *sw, int *sh,
                                const void *pixels)
{
    if (!*slot || *sw != P.coded_w || *sh != P.coded_h) {
        if (*slot) SDL_FreeSurface(*slot);
        *slot = SDL_CreateRGBSurfaceWithFormatFrom(
            (void *)pixels, P.coded_w, P.coded_h, 32, P.coded_w * 4,
            SDL_PIXELFORMAT_ARGB8888);
        *sw = P.coded_w;
        *sh = P.coded_h;
        if (!*slot) return NULL;
    } else {
        (*slot)->pixels = (void *)pixels;
    }
    return *slot;
}

/* Blit the just-acquired frame and record it as the frozen-frame
 * candidate (present() then holds its ring slot frozen - no copy
 * is made anywhere). Returns 1 when the frame was blitted. */
static int blit_frame(SDL_Surface *canvas, const void *pixels)
{
    ps3vdec_stats_t st;
    ps3vdec_get_stats(&st);
    int dw = st.pic_width > 0 ? (int)st.pic_width : P.coded_w;
    int dh = st.pic_height > 0 ? (int)st.pic_height : P.coded_h;
    if (dw > P.coded_w) dw = P.coded_w;
    if (dh > P.coded_h) dh = P.coded_h;
    if (dw <= 0 || dh <= 0) return 0;
    SDL_Surface *surf = wrap_pixels(&P.frame_surface, &P.frame_surf_w,
                                    &P.frame_surf_h, pixels);
    if (!surf) return 0;
    blit_letterboxed(canvas, surf, dw, dh);
    P.last_dw = dw;
    P.last_dh = dh;
    P.last_valid = 1;
    return 1;
}

int yt_player_redraw_last(SDL_Surface *canvas)
{
    if (!canvas || !P.last_valid) return 0;
    if (P.last_dw <= 0 || P.last_dh <= 0) return 0;
    /* The frozen frame is the held ring slot itself (PLAYBACK3
     * fix 2): stable bytes, zero copies. NULL once the slot has
     * been handed back (next acquire / stop). */
    const void *px = ps3vdec_display_frozen();
    if (!px) return 0;
    SDL_Surface *surf = wrap_pixels(&P.frozen_surface, &P.frozen_surf_w,
                                    &P.frozen_surf_h, px);
    if (!surf) return 0;
    blit_letterboxed(canvas, surf, P.last_dw, P.last_dh);
    return 1;
}

int yt_player_present(SDL_Surface *canvas)
{
    if (SDL_AtomicGet(&P.state) != YT_PLAYER_PLAYING || !canvas) return 0;
    /* 1 Hz stats (PLAYBACK3 fix 0): per-second deltas make the
     * next hardware log self-diagnosing - if disp_skipped accrues
     * in bursts aligned with the [source] window lines, the
     * network burst is the trigger; if it accrues uniformly, the
     * presenter is simply over budget. clock-wall is the
     * presentation clock minus wall time since start (it drifts
     * negative across pauses, when the clock is frozen by
     * design). */
    {
        uint64_t t = now_us();
        if (!P.stat_last_us) {
            ps3vdec_stats_t s0;
            ps3vdec_get_stats(&s0);
            P.stat_last_us = t;
            P.stat_last_presented = P.frames_presented;
            P.stat_last_skipped = s0.disp_skipped;
        } else if (t - P.stat_last_us >= 1000000ull) {
            ps3vdec_stats_t st;
            ps3vdec_get_stats(&st);
            uint64_t clk = clock_ns();
            long long drift_ms = clk == UINT64_MAX ? 0
                : (long long)(clk / 1000000ull)
                  - (long long)((t - P.start_us) / 1000ull);
            plog("[yt-player] stats: presented +%d skipped +%u ready=%d clock-wall=%lld ms",
                 P.frames_presented - P.stat_last_presented,
                 (unsigned)(st.disp_skipped - P.stat_last_skipped),
                 ps3vdec_display_ready_count(), drift_ms);
            P.stat_last_us = t;
            P.stat_last_presented = P.frames_presented;
            P.stat_last_skipped = st.disp_skipped;
        }
    }
    uint64_t pts_us;
    if (!ps3vdec_display_next_pts(&pts_us)) {
        if (P.video_eos) {
            uint64_t t = now_us();
            if (!P.end_since_us) P.end_since_us = t;
            int drained = P.audio_absent
                || (P.audio_eos && pcm_available_frames() == 0);
            if (drained && t - P.end_since_us > 700000ull)
                SDL_AtomicSet(&P.state, YT_PLAYER_ENDED);
        }
        return 0;
    }
    P.end_since_us = 0;
    uint64_t clk = clock_ns();
    if (P.frames_presented == 0) {
        /* Preroll: the first frame waits for the audio clock (or
         * proof there is none), so picture and sound start together. */
        int ready = P.anchor_valid || P.audio_absent
            || (now_us() - P.start_us > PREROLL_US);
        if (!ready) return 0;
        if (!P.wall.anchored) yt_wall_anchor(&P.wall, pts_us * 1000ull, now_us());
        if (clk == UINT64_MAX) clk = pts_us * 1000ull;
    }
    if (clk == UINT64_MAX) return 0;
    int shown = 0;
    int iter;
    for (iter = 0; iter < 4; iter++) {
        uint64_t pts_ns = pts_us * 1000ull;
        int act = yt_sync_frame_action(pts_ns, clk);
        if (act == YT_FRAME_HOLD) break;
        const void *px = ps3vdec_display_acquire_pts(&pts_us);
        if (!px) break;
        if (act == YT_FRAME_DROP) {
            ps3vdec_display_release();
            P.sync_drops++;
        } else if (blit_frame(canvas, px)) {
            /* Hold the shown frame's slot frozen (PLAYBACK3
             * fix 2): gap repaints read it back through
             * ps3vdec_display_frozen(); the next acquire hands
             * the slot back to the ring. No copy is made. */
            ps3vdec_display_hold_frozen();
            ps3vdec_display_drop_before(pts_us);
            P.frames_presented++;
            P.last_present_pts_ns = pts_us * 1000ull;
            shown = 1;
        } else {
            ps3vdec_display_release();
        }
        if (!ps3vdec_display_next_pts(&pts_us)) break;
    }
    return shown;
}
