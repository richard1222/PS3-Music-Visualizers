/* yt_demux.h - Neon Lounge's thin facade over mohasi's MP4 demuxer
 * (ps3-dev simple-lib-av, Apache-2.0; see NOTICE). Upstream's demux.h
 * dispatches between MP4 and MKV; YouTube's H.264/AAC formats are
 * always MP4, so only the MP4 branches are kept here. The demuxer
 * itself (demux-mp4.c) is unmodified. */
#ifndef NL_YT_DEMUX_H
#define NL_YT_DEMUX_H

#include "demux-mp4.h"

/* Open a video stream (an fMP4 with an H.264 track; its own AAC track,
 * when present, is queued alongside for takeQueuedAudioAu). The cancel
 * flag is wired into the stream's http source. 0 ok, -1 on failure. */
int yt_demux_open_video(Mp4Demuxer *demuxer, const char *path, volatile int *cancelFlag);

/* Open a standalone audio stream (a DASH audio-only fMP4, e.g. YouTube
 * itag 140), served with readMp4AudioAu. 0 ok, -1 on failure. */
int yt_demux_open_audio(Mp4Demuxer *demuxer, const char *path, volatile int *cancelFlag);

#endif
