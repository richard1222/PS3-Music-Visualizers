/* yt_pick.h - choose the playable streams out of an extractor result
 * (pure, host-tested). Implements PLAN.md section 5's quality policy:
 * the highest H.264 format at or under 720p, preferring <=30 fps (the
 * decoder has the most headroom there), plus the best AAC audio track
 * (itag 140 preferred). Cipher-gated, VP9 and Opus entries are never
 * chosen: the player decodes H.264 + AAC in MP4 only. */
#ifndef NL_YT_PICK_H
#define NL_YT_PICK_H

#include "youtube.h"

typedef struct {
    const StreamFormat *video;   /* chosen video (or muxed) format */
    const StreamFormat *audio;   /* chosen audio format; == video when muxed, NULL when silent */
    int                 muxed;   /* 1 when video carries the audio (one URL serves both) */
} YtPick;

/* 0 when a playable video was found (audio may still be NULL: the video
 * then plays silent), -1 when nothing offered can be played. */
int yt_pick_formats(const StreamInfo *info, YtPick *out);

#endif
