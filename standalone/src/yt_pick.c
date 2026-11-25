/* yt_pick.c - see yt_pick.h. */
#include "yt_pick.h"
#include <string.h>

static int video_playable(const StreamFormat *f)
{
    return f->hasVideo && f->url[0] && !f->needsCipher && !f->isVp9
        && f->height > 0 && f->height <= 720
        && strncmp(f->container, "mp4", 3) == 0;
}

/* Ordering inside one candidate tier: <=30 fps first (an unknown fps
 * counts as 30), then the taller picture, then the wider one. */
static int video_better(const StreamFormat *a, const StreamFormat *b)
{
    int fa = (a->fps > 0 && a->fps <= 30) ? 0 : 1;
    int fb = (b->fps > 0 && b->fps <= 30) ? 0 : 1;
    if (fa != fb) return fa < fb;
    if (a->height != b->height) return a->height > b->height;
    return a->width > b->width;
}

static const StreamFormat *best_video(const StreamInfo *info, int muxed)
{
    const StreamFormat *best = NULL;
    for (int i = 0; i < info->formatCount; i++) {
        const StreamFormat *f = &info->formats[i];
        if (!video_playable(f)) continue;
        if (muxed ? !f->hasAudio : f->hasAudio) continue;
        if (!best || video_better(f, best)) best = f;
    }
    return best;
}

static const StreamFormat *best_audio(const StreamInfo *info)
{
    const StreamFormat *first = NULL;
    for (int i = 0; i < info->formatCount; i++) {
        const StreamFormat *f = &info->formats[i];
        if (!f->hasAudio || f->hasVideo || !f->url[0] || f->needsCipher || f->isOpus) continue;
        if (f->itag == 140) return f;      /* YouTube's canonical AAC-LC track */
        if (!first) first = f;
    }
    if (first) return first;
    /* Multi-language videos list their tracks separately; the formats
     * above may hold none of them. Take the best-ranked track that has
     * a plain AAC URL (rank: ORIGINAL is worth most). */
    const AudioTrack *track = NULL;
    for (int i = 0; i < info->audioTrackCount; i++) {
        const AudioTrack *t = &info->audioTracks[i];
        if (!t->url[0]) continue;
        if (!track || t->rank > track->rank) track = t;
    }
    if (track) {
        /* The track's URL must be handed back as a StreamFormat; the
         * caller only reads url/itag, so keep a static filled copy. */
        static StreamFormat synth;
        memset(&synth, 0, sizeof(synth));
        synth.itag = track->itag;
        synth.hasAudio = 1;
        synth.isDrc = track->isDrc;
        strncpy(synth.container, "mp4", sizeof(synth.container) - 1);
        strncpy(synth.url, track->url, sizeof(synth.url) - 1);
        return &synth;
    }
    return NULL;
}

int yt_pick_formats(const StreamInfo *info, YtPick *out)
{
    out->video = NULL;
    out->audio = NULL;
    out->muxed = 0;
    if (!info) return -1;

    const StreamFormat *v = best_video(info, 0);
    if (v) {
        out->video = v;
        out->audio = best_audio(info);
        return 0;
    }
    v = best_video(info, 1);
    if (v) {
        out->video = v;
        out->audio = v;
        out->muxed = 1;
        return 0;
    }
    return -1;
}
