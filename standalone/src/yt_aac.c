/* yt_aac.c - FAAD2 wrapper, see yt_aac.h. PS3-only (portlibs
 * neaacdec); the host build never links this file. */
#include "yt_aac.h"
#include "yt_adts.h"
#include "youtube.h"     /* yt_log_sink */
#include <neaacdec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AAC_INBUF_BYTES 16384    /* several ADTS frames of headroom */
#define AAC_PTS_QUEUE   32       /* fed-but-not-yet-decoded frame times */

struct YtAac {
    NeAACDecHandle dec;
    int            initialized;  /* NeAACDecInit has run */
    int            failed;
    uint8_t        inbuf[AAC_INBUF_BYTES];
    int            inlen;
    uint64_t       pts_queue[AAC_PTS_QUEUE];
    int            pts_head, pts_tail;
    int            rate;         /* true output rate (post-SBR) */
    int            channels;
};

YtAac *yt_aac_create(void)
{
    YtAac *aac = (YtAac *)calloc(1, sizeof(*aac));
    if (!aac) return NULL;
    aac->dec = NeAACDecOpen();
    if (!aac->dec) { free(aac); return NULL; }
    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(aac->dec);
    if (cfg) {
        cfg->defObjectType = LC;
        cfg->outputFormat = FAAD_FMT_16BIT;   /* native-endian s16: matches AUDIO_S16SYS */
        cfg->downMatrix = 1;                  /* multichannel content folds to stereo */
        cfg->useOldADTSFormat = 0;
        NeAACDecSetConfiguration(aac->dec, cfg);
    }
    return aac;
}

void yt_aac_destroy(YtAac *aac)
{
    if (!aac) return;
    if (aac->dec) NeAACDecClose(aac->dec);
    free(aac);
}

int yt_aac_rate(const YtAac *aac) { return aac ? aac->rate : 0; }
int yt_aac_channels(const YtAac *aac) { return aac ? aac->channels : 0; }

static void pts_push(YtAac *aac, uint64_t pts_ns)
{
    int next = (aac->pts_head + 1) % AAC_PTS_QUEUE;
    if (next == aac->pts_tail) aac->pts_tail = (aac->pts_tail + 1) % AAC_PTS_QUEUE;   /* drop oldest */
    aac->pts_queue[aac->pts_head] = pts_ns;
    aac->pts_head = next;
}

static uint64_t pts_pop(YtAac *aac, uint64_t fallback)
{
    if (aac->pts_head == aac->pts_tail) return fallback;
    uint64_t pts = aac->pts_queue[aac->pts_tail];
    aac->pts_tail = (aac->pts_tail + 1) % AAC_PTS_QUEUE;
    return pts;
}

/* decode everything the input buffer currently yields */
static int aac_drain(YtAac *aac, YtAacPcmFn cb, void *ctx)
{
    while (aac->inlen > 0) {
        NeAACDecFrameInfo info;
        memset(&info, 0, sizeof(info));
        void *out = NeAACDecDecode(aac->dec, &info, aac->inbuf, (unsigned long)aac->inlen);
        if (info.bytesconsumed == 0) {
            if (info.error) {
                /* cannot make progress on this byte: skip it to resync */
                memmove(aac->inbuf, aac->inbuf + 1, (size_t)(aac->inlen - 1));
                aac->inlen--;
                continue;
            }
            break;   /* needs more input */
        }
        int consumed = (int)info.bytesconsumed;
        if (consumed > aac->inlen) consumed = aac->inlen;
        memmove(aac->inbuf, aac->inbuf + consumed, (size_t)(aac->inlen - consumed));
        aac->inlen -= consumed;
        if (info.error) {
            char line[160];
            snprintf(line, sizeof(line), "[yt-aac] decode error %u (%s), frame dropped",
                     info.error, NeAACDecGetErrorMessage(info.error));
            yt_log_sink("warn", line);
            continue;
        }
        if (out && info.samples > 0 && info.channels > 0) {
            if (!aac->rate) {
                aac->rate = (int)info.samplerate;
                aac->channels = (int)info.channels;
                char line[128];
                snprintf(line, sizeof(line), "[yt-aac] faad2 up: %d Hz, %d channel(s)",
                         aac->rate, aac->channels);
                yt_log_sink("info", line);
            }
            int frames = (int)(info.samples / info.channels);
            uint64_t pts = pts_pop(aac, 0);
            if (cb) cb(ctx, (const int16_t *)out, frames, (int)info.channels, pts);
        } else {
            pts_pop(aac, 0);   /* a silent/concealed frame still consumed one input frame */
        }
    }
    return 0;
}

int yt_aac_feed(YtAac *aac, const uint8_t *frame, int size, uint64_t pts_ns,
                int core_rate, int channels, YtAacPcmFn cb, void *ctx)
{
    if (!aac || aac->failed) return -1;
    uint8_t adts[YT_ADTS_HEADER_BYTES];
    if (yt_adts_header(adts, size, core_rate, channels) != 0) return -1;
    int total = YT_ADTS_HEADER_BYTES + size;
    if (total > AAC_INBUF_BYTES) return -1;
    if (aac->inlen + total > AAC_INBUF_BYTES) {
        /* make room by decoding what's buffered; a still-full buffer
         * means the decoder is stuck - fail the audio, keep the video */
        aac_drain(aac, cb, ctx);
        if (aac->inlen + total > AAC_INBUF_BYTES) { aac->failed = 1; return -1; }
    }
    memcpy(aac->inbuf + aac->inlen, adts, YT_ADTS_HEADER_BYTES);
    memcpy(aac->inbuf + aac->inlen + YT_ADTS_HEADER_BYTES, frame, (size_t)size);
    aac->inlen += total;
    pts_push(aac, pts_ns);

    if (!aac->initialized) {
        unsigned long rate = 0;
        unsigned char ch = 0;
        long used = NeAACDecInit(aac->dec, aac->inbuf, (unsigned long)aac->inlen, &rate, &ch);
        aac->initialized = 1;
        if (used < 0) {
            yt_log_sink("error", "[yt-aac] NeAACDecInit failed");
            aac->failed = 1;
            return -1;
        }
        /* Init only parses the first header; the frame itself is still
         * decoded from the buffer head by the drain below. */
    }
    return aac_drain(aac, cb, ctx);
}
