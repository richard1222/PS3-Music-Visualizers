/* yt_player.h - YouTube playback engine (PLAN.md milestones M2+M3).
 *
 * Owns everything between the M1 extractor and the screen: the
 * mohasi fMP4 demuxers over the ranged video source, the local
 * ps3vdec H.264 decode, FAAD2 audio (yt_aac), and the audio-master
 * presentation clock (yt_sync). Two worker threads (video feed,
 * audio) plus the main thread presenting; the SDL audio callback is
 * the third consumer and only ever reads the PCM ring.
 *
 * Freeze rule: every wait is bounded, both workers watch a cancel
 * flag (the network sources watch it too, so a stalled fetch dies
 * with the stop), and yt_player_stop() joins with deadlines - the
 * app's exit path never depends on this engine finishing cleanly.
 */
#ifndef NL_YT_PLAYER_H
#define NL_YT_PLAYER_H

#include <SDL.h>
#include <stdint.h>
#include "youtube.h"   /* StreamInfo */

enum {
    YT_PLAYER_IDLE = 0,
    YT_PLAYER_STARTING,
    YT_PLAYER_PLAYING,
    YT_PLAYER_ENDED,
    YT_PLAYER_ERROR
};

/* Begin playback of an extracted video. Copies what it needs.
 * Returns 0 when the workers are running (state STARTING, then
 * PLAYING / ERROR / ENDED); the demux/decoder open happens on the
 * feed thread so the UI never blocks on the network. */
int         yt_player_start(const StreamInfo *info, const char *video_id);
void        yt_player_stop(void);                 /* bounded, idempotent */
int         yt_player_state(void);
const char *yt_player_error(void);                /* valid in ERROR state */
const char *yt_player_title(void);

/* Audio device handshake with main.c: the device must be opened at
 * FAAD2's true output rate, which is only known after the first
 * decoded frame. Poll yt_player_audio_format() each frame; when it
 * returns 1, open the device at that rate/channels and call
 * yt_player_set_output_open(1). When it returns -1 the video has no
 * usable audio and plays against the wall clock. */
int         yt_player_audio_format(int *rate, int *channels);
void        yt_player_set_output_open(int on);

/* SDL audio callback source: copies up to `samples` interleaved s16
 * values into dst and returns how many it filled (0 = underrun,
 * caller silences the rest). Lock-free enough for the callback:
 * one short mutex, no allocation, no logging. */
int         yt_player_audio_read(int16_t *dst, int samples);

void        yt_player_set_paused(int paused);
int         yt_player_paused(void);

/* OSD data (nanoseconds; duration 0 = unknown). */
uint64_t    yt_player_position_ns(void);
uint64_t    yt_player_duration_ns(void);
int         yt_player_buffering(void);   /* no frame presented yet */

/* Main thread, once per frame while the player view is up: applies
 * the sync decision, claims the due frame and blits it into the
 * canvas (letterboxed). Returns 1 when a new frame was shown.
 * Also advances the ENDED state when the streams run out. */
int         yt_player_present(SDL_Surface *canvas);

/* Repaint the last presented frame (letterboxed, same geometry
 * as present). The playing view calls this when present() showed
 * nothing, so a present gap - HOLD, preroll, rebuffer, pause -
 * shows the frozen frame instead of black (PLAYBACK2 fix 2). The
 * frozen frame is the presented frame's own decoder ring slot,
 * held by ps3vdec (PLAYBACK3 fix 2 - no copy is made). Returns 1
 * when a frame was repainted, 0 when none has been shown yet. */
int         yt_player_redraw_last(SDL_Surface *canvas);

#endif
