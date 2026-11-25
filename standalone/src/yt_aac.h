/* yt_aac.h - AAC decoding for the YouTube player: a small wrapper
 * over portlibs FAAD2, behind the 3-function interface PLAN.md
 * section 5 asks for (so a future cellAdec swap stays cheap).
 *
 * The demuxer hands over raw AAC frames; each is wrapped in an ADTS
 * header (yt_adts.h - the demuxer's adtsRate is the core rate the
 * header must carry) and fed to FAAD2 as one continuous stream.
 * Decoded PCM comes back through a callback as interleaved s16 at
 * the decoder's true output rate (post-SBR for HE-AAC). */
#ifndef NL_YT_AAC_H
#define NL_YT_AAC_H

#include <stdint.h>

typedef struct YtAac YtAac;

/* Called once per decoded frame: interleaved s16 samples (frames *
 * channels values), the frame's stream presentation time. */
typedef void (*YtAacPcmFn)(void *ctx, const int16_t *samples, int frames, int channels, uint64_t pts_ns);

YtAac *yt_aac_create(void);
void   yt_aac_destroy(YtAac *aac);

/* Feed one raw AAC frame (no ADTS) with its presentation time.
 * Decoded frames are delivered to cb as they emerge (in order).
 * 0 ok, -1 on a decoder error (the stream then plays silent). */
int    yt_aac_feed(YtAac *aac, const uint8_t *frame, int size, uint64_t pts_ns,
                   int core_rate, int channels, YtAacPcmFn cb, void *ctx);

/* Output format, valid after the first decoded frame (0 before). */
int    yt_aac_rate(const YtAac *aac);
int    yt_aac_channels(const YtAac *aac);

#endif
