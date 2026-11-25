/* yt_sync.c - see yt_sync.h. */
#include "yt_sync.h"

int yt_sync_frame_action(uint64_t frame_pts_ns, uint64_t clock_ns)
{
    if (frame_pts_ns > clock_ns) {
        return (frame_pts_ns - clock_ns > YT_SYNC_HOLD_EARLY_NS)
               ? YT_FRAME_HOLD : YT_FRAME_PRESENT;
    }
    return (clock_ns - frame_pts_ns > YT_SYNC_DROP_LATE_NS)
           ? YT_FRAME_DROP : YT_FRAME_PRESENT;
}

uint64_t yt_sync_feed_ahead_ns(int fps)
{
    if (fps <= 0) fps = 30;
    uint64_t ahead = 4ull * (1000000000ull / (uint64_t)fps);
    if (ahead < 120000000ull) ahead = 120000000ull;
    if (ahead > 200000000ull) ahead = 200000000ull;
    return ahead;
}

void yt_sync_letterbox(int cw, int ch, int dw, int dh,
                       int *dx, int *dy, int *ow, int *oh)
{
    double scale = (double)cw / (double)dw;
    double s2 = (double)ch / (double)dh;
    if (s2 < scale) scale = s2;
    int w = (int)(dw * scale);
    int h = (int)(dh * scale);
    if (ow) *ow = w;
    if (oh) *oh = h;
    if (dx) *dx = (cw - w) / 2;
    if (dy) *dy = (ch - h) / 2;
}

uint64_t yt_sync_audio_clock_ns(uint64_t anchor_pts_ns, uint64_t played_frames, uint32_t rate)
{
    if (!rate) return anchor_pts_ns;
    return anchor_pts_ns + played_frames * 1000000000ull / rate;
}

void yt_wall_init(YtWallClock *w)
{
    w->base_pts_ns = 0;
    w->base_wall_us = 0;
    w->paused_since_us = 0;
    w->anchored = 0;
    w->paused = 0;
}

void yt_wall_anchor(YtWallClock *w, uint64_t pts_ns, uint64_t now_us)
{
    w->base_pts_ns = pts_ns;
    w->base_wall_us = now_us;
    w->anchored = 1;
    if (w->paused) w->paused_since_us = now_us;
}

uint64_t yt_wall_now(const YtWallClock *w, uint64_t now_us)
{
    if (!w->anchored) return 0;
    uint64_t end = w->paused ? w->paused_since_us : now_us;
    if (end < w->base_wall_us) return w->base_pts_ns;
    return w->base_pts_ns + (end - w->base_wall_us) * 1000ull;
}

void yt_wall_set_paused(YtWallClock *w, int paused, uint64_t now_us)
{
    if (paused && !w->paused) {
        w->paused = 1;
        w->paused_since_us = now_us;
    } else if (!paused && w->paused) {
        w->paused = 0;
        if (w->anchored && now_us >= w->paused_since_us)
            w->base_wall_us += now_us - w->paused_since_us;   /* paused time never happened */
    }
}
