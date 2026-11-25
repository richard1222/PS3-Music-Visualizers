/* Host tests for the YouTube playback milestone's pure logic:
 * the format picker (src/yt_pick.c), the audio-master sync math
 * (src/yt_sync.c) and the ADTS header builder (src/yt_adts.c).
 * The threaded engine, demuxer and decoders are exercised by
 * tests/test_yt_demux.c and on hardware. */
#include "yt_pick.h"
#include "yt_sync.h"
#include "yt_adts.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static StreamFormat *add_format(StreamInfo *info, int itag, int w, int h, int fps,
                                int has_video, int has_audio, int vp9, int opus,
                                const char *container)
{
    StreamFormat *f = &info->formats[info->formatCount++];
    memset(f, 0, sizeof(*f));
    f->itag = itag;
    f->width = w;
    f->height = h;
    f->fps = fps;
    f->hasVideo = has_video;
    f->hasAudio = has_audio;
    f->isVp9 = vp9;
    f->isOpus = opus;
    snprintf(f->container, sizeof(f->container), "%s", container);
    snprintf(f->url, sizeof(f->url), "https://example.invalid/%d", itag);
    return f;
}

static void test_picker_typical(void)
{
    StreamInfo info;
    memset(&info, 0, sizeof(info));
    add_format(&info, 18, 640, 360, 30, 1, 1, 0, 0, "mp4");    /* muxed 360p */
    add_format(&info, 134, 640, 360, 30, 1, 0, 0, 0, "mp4");
    add_format(&info, 135, 854, 480, 30, 1, 0, 0, 0, "mp4");
    add_format(&info, 136, 1280, 720, 30, 1, 0, 0, 0, "mp4");
    add_format(&info, 137, 1920, 1080, 30, 1, 0, 0, 0, "mp4"); /* over the cap */
    add_format(&info, 298, 1280, 720, 60, 1, 0, 0, 0, "mp4");   /* 60 fps tier */
    add_format(&info, 242, 640, 360, 30, 1, 0, 1, 0, "webm");   /* VP9 */
    add_format(&info, 140, 0, 0, 0, 0, 1, 0, 0, "mp4");         /* AAC */
    add_format(&info, 251, 0, 0, 0, 0, 1, 0, 1, "webm");        /* Opus */
    YtPick pick;
    CHECK(yt_pick_formats(&info, &pick) == 0);
    CHECK(pick.video && pick.video->itag == 136);   /* 720p30 beats 720p60 and 480p */
    CHECK(pick.audio && pick.audio->itag == 140);
    CHECK(pick.muxed == 0);
}

static void test_picker_muxed_fallback(void)
{
    StreamInfo info;
    memset(&info, 0, sizeof(info));
    add_format(&info, 137, 1920, 1080, 30, 1, 0, 0, 0, "mp4");  /* too tall */
    add_format(&info, 18, 640, 360, 30, 1, 1, 0, 0, "mp4");     /* only muxed left */
    YtPick pick;
    CHECK(yt_pick_formats(&info, &pick) == 0);
    CHECK(pick.video && pick.video->itag == 18);
    CHECK(pick.audio == pick.video);
    CHECK(pick.muxed == 1);
}

static void test_picker_cipher_and_tracks(void)
{
    StreamInfo info;
    memset(&info, 0, sizeof(info));
    StreamFormat *v = add_format(&info, 136, 1280, 720, 30, 1, 0, 0, 0, "mp4");
    v->needsCipher = 1;                                          /* unusable */
    add_format(&info, 135, 854, 480, 30, 1, 0, 0, 0, "mp4");
    /* no plain AAC format: two language tracks, the ORIGINAL-ranked wins */
    info.audioTrackCount = 2;
    snprintf(info.audioTracks[0].url, sizeof(info.audioTracks[0].url), "https://example.invalid/dub");
    info.audioTracks[0].itag = 140;
    info.audioTracks[0].rank = 1;
    snprintf(info.audioTracks[1].url, sizeof(info.audioTracks[1].url), "https://example.invalid/orig");
    info.audioTracks[1].itag = 140;
    info.audioTracks[1].rank = 5;
    YtPick pick;
    CHECK(yt_pick_formats(&info, &pick) == 0);
    CHECK(pick.video && pick.video->itag == 135);
    CHECK(pick.audio && strstr(pick.audio->url, "orig") != NULL);
}

static void test_picker_nothing(void)
{
    StreamInfo info;
    memset(&info, 0, sizeof(info));
    add_format(&info, 137, 1920, 1080, 30, 1, 0, 0, 0, "mp4");
    add_format(&info, 251, 0, 0, 0, 0, 1, 0, 1, "webm");
    YtPick pick;
    CHECK(yt_pick_formats(&info, &pick) != 0);
    CHECK(yt_pick_formats(NULL, &pick) != 0);
}

static void test_sync(void)
{
    const uint64_t MS = 1000000ull;
    /* frame action boundaries: hold >40 ms early, drop >80 ms late */
    CHECK(yt_sync_frame_action(100 * MS, 100 * MS) == YT_FRAME_PRESENT);
    CHECK(yt_sync_frame_action(140 * MS, 100 * MS) == YT_FRAME_PRESENT);
    CHECK(yt_sync_frame_action(141 * MS, 100 * MS) == YT_FRAME_HOLD);
    CHECK(yt_sync_frame_action(20 * MS, 100 * MS) == YT_FRAME_PRESENT);
    CHECK(yt_sync_frame_action(19 * MS, 100 * MS) == YT_FRAME_DROP);
    CHECK(yt_sync_frame_action(0, 100 * MS) == YT_FRAME_DROP);

    /* audio clock: anchor + played frames at the output rate */
    CHECK(yt_sync_audio_clock_ns(1000 * MS, 0, 48000) == 1000 * MS);
    CHECK(yt_sync_audio_clock_ns(1000 * MS, 48000, 48000) == 2000 * MS);
    CHECK(yt_sync_audio_clock_ns(0, 22050, 44100) == 500 * MS);
    CHECK(yt_sync_audio_clock_ns(0, 48000, 0) == 0);   /* no rate: anchor only */

    /* wall clock: anchor, pause, resume */
    YtWallClock w;
    memset(&w, 0, sizeof(w));
    CHECK(yt_wall_now(&w, 1234) == 0);                 /* unanchored reads 0 */
    yt_wall_anchor(&w, 500 * MS, 100000);              /* t=100 ms wall */
    CHECK(yt_wall_now(&w, 100000) == 500 * MS);
    CHECK(yt_wall_now(&w, 300000) == 700 * MS);
    yt_wall_set_paused(&w, 1, 300000);
    CHECK(yt_wall_now(&w, 900000) == 700 * MS);        /* frozen while paused */
    yt_wall_set_paused(&w, 0, 900000);
    CHECK(yt_wall_now(&w, 1000000) == 800 * MS);       /* pause span excluded */
    /* pausing before anchoring is harmless */
    YtWallClock w2;
    memset(&w2, 0, sizeof(w2));
    yt_wall_set_paused(&w2, 1, 5000);
    yt_wall_anchor(&w2, 0, 6000);
    CHECK(yt_wall_now(&w2, 999999) == 0);
    yt_wall_set_paused(&w2, 0, 1000000);
    CHECK(yt_wall_now(&w2, 1500000) == 500 * MS);
}

static void test_feed_ahead(void)
{
    const uint64_t MS = 1000000ull;
    /* PLAYBACK2 fix 1: the feed window is four frame intervals,
     * clamped to [120 ms, 200 ms] - never the old flat 1.5 s. */
    CHECK(yt_sync_feed_ahead_ns(30) == 133333332ull);
    CHECK(yt_sync_feed_ahead_ns(25) == 160 * MS);
    CHECK(yt_sync_feed_ahead_ns(24) == 166666664ull);
    CHECK(yt_sync_feed_ahead_ns(60) == 120 * MS);   /* floor: 4 frames is 67 ms */
    CHECK(yt_sync_feed_ahead_ns(50) == 120 * MS);   /* floor */
    CHECK(yt_sync_feed_ahead_ns(15) == 200 * MS);   /* ceiling: 4 frames is 267 ms */
    CHECK(yt_sync_feed_ahead_ns(12) == 200 * MS);   /* ceiling */
    CHECK(yt_sync_feed_ahead_ns(0) == yt_sync_feed_ahead_ns(30));   /* unknown fps -> 30 */
    CHECK(yt_sync_feed_ahead_ns(-5) == yt_sync_feed_ahead_ns(30));
}

static void test_letterbox(void)
{
    int dx, dy, ow, oh;
    /* PLAYBACK3 fix 3: the 1:1 cases take the plain-copy blit
     * path (ow == dw && oh == dh). 960x720 into the 1280x720
     * canvas is the hw6 case: height pins the scale to exactly
     * 1.0, pillarboxed. */
    yt_sync_letterbox(1280, 720, 960, 720, &dx, &dy, &ow, &oh);
    CHECK(ow == 960 && oh == 720 && dx == 160 && dy == 0);
    /* native size: exact fit, no offset */
    yt_sync_letterbox(1280, 720, 1280, 720, &dx, &dy, &ow, &oh);
    CHECK(ow == 1280 && oh == 720 && dx == 0 && dy == 0);
    /* scaled cases stay off the 1:1 path */
    yt_sync_letterbox(1280, 720, 640, 360, &dx, &dy, &ow, &oh);
    CHECK(ow == 1280 && oh == 720 && dx == 0 && dy == 0);
    CHECK(!(ow == 640 && oh == 360));
    yt_sync_letterbox(1280, 720, 640, 480, &dx, &dy, &ow, &oh);
    CHECK(ow == 960 && oh == 720 && dx == 160 && dy == 0);   /* scale 1.5 */
    yt_sync_letterbox(1280, 720, 320, 240, &dx, &dy, &ow, &oh);
    CHECK(ow == 960 && oh == 720 && dx == 160 && dy == 0);   /* scale 3 */
}

static void test_adts(void)
{
    uint8_t h[YT_ADTS_HEADER_BYTES];
    CHECK(yt_adts_header(h, 200, 44100, 2) == 0);
    CHECK(h[0] == 0xff && (h[1] & 0xf0) == 0xf0);      /* syncword */
    CHECK((h[1] & 0x01) == 1);                          /* protection_absent: no CRC */
    int len = ((h[3] & 0x03) << 11) | (h[4] << 3) | ((h[5] & 0xe0) >> 5);
    CHECK(len == 200 + YT_ADTS_HEADER_BYTES);           /* frame length field */
    int rate_idx = (h[2] >> 2) & 0x0f;
    CHECK(rate_idx == 4);                               /* 44100 */
    int profile = (h[2] >> 6) & 0x03;
    CHECK(profile == 1);                                /* LC */
    int chan = ((h[2] & 0x01) << 2) | ((h[3] >> 6) & 0x03);
    CHECK(chan == 2);
    CHECK(yt_adts_header(h, 100, 48000, 1) == 0);
    CHECK(((h[2] >> 2) & 0x0f) == 3);                   /* 48000 */
    CHECK(yt_adts_header(h, 100, 12345, 2) != 0);       /* unknown rate */
    CHECK(yt_adts_header(h, 0, 44100, 2) != 0);         /* empty frame */
}

int main(void)
{
    test_picker_typical();
    test_picker_muxed_fallback();
    test_picker_cipher_and_tracks();
    test_picker_nothing();
    test_sync();
    test_feed_ahead();
    test_letterbox();
    test_adts();
    if (failures) { printf("test_yt_playback: %d FAILURES\n", failures); return 1; }
    printf("test_yt_playback: all checks passed\n");
    return 0;
}
