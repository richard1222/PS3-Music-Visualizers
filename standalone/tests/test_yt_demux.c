/* Host tests for the YouTube player's demux layer (src/yt_demux.c
 * over the ported mohasi demuxer + video source, stdio backend):
 * opens ffmpeg-generated fragmented MP4 fixtures - the same shape
 * YouTube serves - and walks their access units.
 *
 * Fixtures are made by tests/make_yt_fixtures.sh into the directory
 * named by argv[1] (default build/ytfx). */
#include "yt_demux.h"
#include "demux-mp4.h"
#include "demux-types.h"
#include "h264.h"
#include "youtube.h"   /* yt_log_sink declaration */
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

/* yt_demux.c logs through the app's sink; print it on the host. */
void yt_log_sink(const char *tag, const char *line)
{
    printf("[%s] %s\n", tag, line);
}

static void path_join(char *out, size_t cap, const char *dir, const char *name)
{
    snprintf(out, cap, "%s/%s", dir, name);
}

static void test_muxed(const char *dir)
{
    char path[512];
    path_join(path, sizeof(path), dir, "muxfrag.mp4");
    Mp4Demuxer d;
    memset(&d, 0, sizeof(d));
    CHECK(yt_demux_open_video(&d, path, NULL) == 0);
    CHECK(d.width == 640 && d.height == 360);
    CHECK(d.hasAudio == 1);
    CHECK(d.durationNs > 1500000000ull && d.durationNs < 2600000000ull);
    H264StreamInfo si;
    memset(&si, 0, sizeof(si));
    CHECK(readH264StreamInfo(d.h264.header, d.h264.headerSize, &si) == 0);
    /* coded size, not display size: 360 lines are coded as 368 */
    CHECK(si.codedWidth == 640 && si.codedHeight == 368);

    VideoAu au;
    int rc, count = 0;
    uint64_t last_pts = 0;
    while ((rc = readMp4VideoAu(&d, &au)) == 1) {
        CHECK(au.size > 0 && au.data != NULL);
        if (count == 0) {
            CHECK(au.keyframe == 1);
            /* B-frame reorder: the first AU in decode order presents
             * a couple of frames in, not at 0 */
            CHECK(au.pts < 200000000ull);
        }
        last_pts = au.pts;
        count++;
    }
    CHECK(rc == 0);                                   /* clean end of stream */
    CHECK(count >= 50 && count <= 70);                /* 2 s at 30 fps */
    CHECK(last_pts > 1800000000ull && last_pts < 2100000000ull);

    /* The muxed audio was pumped alongside the video reads. The
     * queue holds 64 AUs and the pump pauses while it is full (the
     * player drains it concurrently; this test reads video first),
     * so expect a full queue's worth, not the whole ~86 frames. */
    AudioAu aau;
    int acount = 0;
    while (takeQueuedAudioAu(&d.audioQueue, &aau) == 1) {
        CHECK(aau.size > 0 && aau.size <= AUDIO_AU_MAX_BYTES);
        acount++;
    }
    CHECK(acount >= 55 && acount <= 70);
    closeMp4Demuxer(&d);
}

static void test_video_only(const char *dir)
{
    char path[512];
    path_join(path, sizeof(path), dir, "vfrag.mp4");
    Mp4Demuxer d;
    memset(&d, 0, sizeof(d));
    CHECK(yt_demux_open_video(&d, path, NULL) == 0);
    CHECK(d.hasAudio == 0);
    VideoAu au;
    int rc, count = 0;
    while ((rc = readMp4VideoAu(&d, &au)) == 1) count++;
    CHECK(rc == 0);
    CHECK(count >= 50 && count <= 70);
    closeMp4Demuxer(&d);
}

static void test_audio_only(const char *dir)
{
    char path[512];
    path_join(path, sizeof(path), dir, "afrag.m4a");
    Mp4Demuxer d;
    memset(&d, 0, sizeof(d));
    CHECK(yt_demux_open_audio(&d, path, NULL) == 0);
    CHECK(d.audioAdtsRate == 44100);
    AudioAu au;
    int rc, count = 0;
    while ((rc = readMp4AudioAu(&d, &au)) == 1) {
        CHECK(au.size > 0);
        count++;
    }
    CHECK(rc == 0);
    CHECK(count >= 60 && count <= 110);
    closeMp4Demuxer(&d);
}

static void test_missing(const char *dir)
{
    char path[512];
    path_join(path, sizeof(path), dir, "does-not-exist.mp4");
    Mp4Demuxer d;
    memset(&d, 0, sizeof(d));
    CHECK(yt_demux_open_video(&d, path, NULL) != 0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/ytfx";
    test_muxed(dir);
    test_video_only(dir);
    test_audio_only(dir);
    test_missing(dir);
    if (failures) { printf("test_yt_demux: %d FAILURES\n", failures); return 1; }
    printf("test_yt_demux: all checks passed\n");
    return 0;
}
