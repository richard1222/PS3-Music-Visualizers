/* yt_sync.h - the YouTube player's A/V sync math (pure, host-tested).
 *
 * The sync design is mohasi's audio-master clock (ps3-dev / Yo! Player,
 * Apache-2.0; see NOTICE): video paces against the PCM the audio device
 * has actually consumed, so the two cannot drift. The thresholds are
 * PLAN.md section 5: hold a frame that is more than 40 ms early, drop
 * one that is more than 80 ms late. This file holds only the arithmetic
 * so tests/test_yt_playback.c can pin it down off-device. */
#ifndef NL_YT_SYNC_H
#define NL_YT_SYNC_H

#include <stdint.h>

#define YT_SYNC_HOLD_EARLY_NS 40000000ull   /* frame further ahead than this: wait */
#define YT_SYNC_DROP_LATE_NS  80000000ull   /* frame further behind than this: skip it */

enum { YT_FRAME_HOLD = 0, YT_FRAME_PRESENT = 1, YT_FRAME_DROP = 2 };

/* Decide what to do with a frame of presentation time frame_pts_ns
 * against the master clock clock_ns. */
int yt_sync_frame_action(uint64_t frame_pts_ns, uint64_t clock_ns);

/* Feed pacing window (PLAYBACK2-DIAGNOSIS fix 1): how far ahead of
 * the presentation clock the feed thread may release access units
 * to the decoder. Four frame intervals, clamped to [120 ms,
 * 200 ms]. The 1.13 build used a flat 1.5 s: the decoder raced
 * through the window, the 4-slot display ring trashed the ~41
 * frames that were about to come due, and presentation collapsed
 * to ~4-frame bursts every ~1.4 s. With the window at a few frame
 * intervals the ring holds exactly the next due frames. fps <= 0
 * (unknown) is treated as 30. */
uint64_t yt_sync_feed_ahead_ns(int fps);

/* Letterbox geometry for presenting a dw x dh picture on a cw x ch
 * canvas: the largest same-aspect rect that fits, centered. The
 * player's blit uses these exact numbers, and when the result
 * equals the picture size (ow == dw && oh == dh) the blit is 1:1
 * and takes the plain-copy fast path instead of the generic
 * software scaler (PLAYBACK3 fix 3 - at 960x720 into a 1280x720
 * canvas the scale is exactly 1.0, the common native-size case).
 * Pure arithmetic, host-tested. */
void yt_sync_letterbox(int cw, int ch, int dw, int dh,
                       int *dx, int *dy, int *ow, int *oh);

/* The audio clock: the stream position whose PCM is reaching the
 * speakers now. anchor_pts_ns is the presentation time of the first
 * frame ever queued; played_frames counts frames (per channel) the
 * audio callback has actually taken from the player's ring since. */
uint64_t yt_sync_audio_clock_ns(uint64_t anchor_pts_ns, uint64_t played_frames, uint32_t rate);

/* A wall clock in stream-pts terms, for silent videos and for pacing
 * before the audio clock anchors. Paused spans are excluded. */
typedef struct {
    uint64_t base_pts_ns;       /* stream position at base_wall_us */
    uint64_t base_wall_us;      /* wall time (microseconds) of the anchor */
    uint64_t paused_since_us;   /* wall time the current pause began */
    int      anchored;
    int      paused;
} YtWallClock;

void     yt_wall_init(YtWallClock *w);
void     yt_wall_anchor(YtWallClock *w, uint64_t pts_ns, uint64_t now_us);
uint64_t yt_wall_now(const YtWallClock *w, uint64_t now_us);
void     yt_wall_set_paused(YtWallClock *w, int paused, uint64_t now_us);

#endif
