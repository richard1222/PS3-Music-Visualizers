/* Host tests for the YouTube extractor port (src/youtube.c):
 * the pure parse halves against canned InnerTube replies, and the
 * visitor-session lifecycle against a scratch directory. The network
 * half is stubbed out on the host (yt_net.c), exactly like the update
 * tests stub theirs. */
#include "youtube.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

/* A trimmed WEB search reply: two videoRenderers and a continuation. */
static const char SEARCH_JSON[] =
    "{\"responseContext\":{\"visitorData\":\"CgtUZXN0\"},\"contents\":{"
    "\"sectionListRenderer\":{\"contents\":[{\"itemSectionRenderer\":{\"contents\":["
    "{\"videoRenderer\":{\"videoId\":\"abc123XYZ_-\","
    "\"title\":{\"runs\":[{\"text\":\"Test Song One\"}]},"
    "\"lengthText\":{\"simpleText\":\"3:45\"},"
    "\"ownerText\":{\"runs\":[{\"text\":\"Channel One\"}]},"
    "\"viewCountText\":{\"simpleText\":\"1,234 views\"},"
    "\"publishedTimeText\":{\"simpleText\":\"2 days ago\"},"
    "\"navigationEndpoint\":{\"browseEndpoint\":{\"browseId\":\"UCchannelone\"}}}},"
    "{\"videoRenderer\":{\"videoId\":\"def456UVW_9\","
    "\"title\":{\"runs\":[{\"text\":\"Second Video\"}]},"
    "\"lengthText\":{\"simpleText\":\"10:01\"},"
    "\"ownerText\":{\"runs\":[{\"text\":\"Channel Two\"}]},"
    "\"viewCountText\":{\"simpleText\":\"98 views\"},"
    "\"publishedTimeText\":{\"simpleText\":\"1 hour ago\"}}}"
    "]}},{\"continuationItemRenderer\":{\"continuationEndpoint\":{"
    "\"continuationCommand\":{\"token\":\"CONT123\"}}}}]}}}";

/* A trimmed VISIONOS player reply: one progressive format, adaptive
 * H.264 at 720p/1080p, AAC and Opus audio. */
static const char PLAYER_JSON[] =
    "{\"playabilityStatus\":{\"status\":\"OK\"},"
    "\"videoDetails\":{\"videoId\":\"abc123XYZ_-\",\"title\":\"Test Song One\","
    "\"lengthSeconds\":\"225\",\"shortDescription\":\"A test video\"},"
    "\"streamingData\":{"
    "\"formats\":[{\"itag\":18,\"mimeType\":\"video/mp4; codecs=\\\"avc1.42001E, mp4a.40.2\\\"\","
    "\"width\":640,\"height\":360,\"url\":\"https://video.example/p18\"}],"
    "\"adaptiveFormats\":["
    "{\"itag\":136,\"mimeType\":\"video/mp4; codecs=\\\"avc1.64001f\\\"\","
    "\"width\":1280,\"height\":720,\"fps\":30,\"url\":\"https://video.example/v136\"},"
    "{\"itag\":137,\"mimeType\":\"video/mp4; codecs=\\\"avc1.640028\\\"\","
    "\"width\":1920,\"height\":1080,\"fps\":30,\"url\":\"https://video.example/v137\"},"
    "{\"itag\":140,\"mimeType\":\"audio/mp4; codecs=\\\"mp4a.40.2\\\"\","
    "\"url\":\"https://video.example/a140\"},"
    "{\"itag\":251,\"mimeType\":\"audio/webm; codecs=\\\"opus\\\"\","
    "\"url\":\"https://video.example/a251\"}"
    "]}}";

static void test_search_parse(void)
{
    static SearchResults out;
    int n = yt_parse_results(SEARCH_JSON, (int)strlen(SEARCH_JSON), &out);
    CHECK(n == 2);
    CHECK(out.count == 2);
    CHECK(strcmp(out.items[0].videoId, "abc123XYZ_-") == 0);
    CHECK(strcmp(out.items[0].title, "Test Song One") == 0);
    CHECK(strcmp(out.items[0].duration, "3:45") == 0);
    CHECK(strcmp(out.items[0].author, "Channel One") == 0);
    CHECK(strcmp(out.items[0].views, "1,234 views") == 0);
    CHECK(strcmp(out.items[0].published, "2 days ago") == 0);
    CHECK(strcmp(out.items[0].channelId, "UCchannelone") == 0);
    CHECK(out.items[0].isLive == 0);
    CHECK(strcmp(out.items[1].videoId, "def456UVW_9") == 0);
    CHECK(strcmp(out.items[1].title, "Second Video") == 0);
    CHECK(strcmp(out.continuation, "CONT123") == 0);
}

static void test_player_parse(void)
{
    static StreamInfo info;
    int rc = yt_parse_player(PLAYER_JSON, (int)strlen(PLAYER_JSON), &info);
    CHECK(rc == 0);
    CHECK(strcmp(info.title, "Test Song One") == 0);
    CHECK(info.durationSeconds == 225);
    CHECK(info.isLive == 0);
    CHECK(info.formatCount == 5);
    int best_video = -1, best_height = -1, aac = -1;
    for (int i = 0; i < info.formatCount; i++) {
        const StreamFormat *f = &info.formats[i];
        CHECK(f->url[0] != 0);
        CHECK(f->needsCipher == 0);
        if (f->hasVideo && !f->hasAudio && f->height <= 720 && f->height > best_height) {
            best_height = f->height;
            best_video = f->itag;
        }
        if (f->hasAudio && !f->hasVideo && !f->isOpus) aac = f->itag;
    }
    /* the M3 quality policy picks: highest H.264 at or under 720p + the AAC track */
    CHECK(best_video == 136);
    CHECK(aac == 140);
}

static void test_visitor_lifecycle(void)
{
    const char *dir = "build/test-yt-data";
    char path[256], tmp[256], buf[256];
    mkdir("build", 0755);
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/visitor.txt", dir);
    snprintf(tmp, sizeof(tmp), "%s/visitor.txt.tmp", dir);
    remove(path);
    remove(tmp);

    yt_set_data_dir(dir);
    CHECK(yt_visitor_persisted() == 0);
    CHECK(yt_visitor_load(buf, sizeof(buf)) == 0);

    CHECK(yt_visitor_store("SESSION_TOKEN_ABC=") == 0);
    CHECK(yt_visitor_persisted() == 1);
    /* the atomic write must not leave its temp file behind */
    FILE *f = fopen(tmp, "rb");
    CHECK(f == NULL);
    if (f) fclose(f);
    /* and the file content is exactly the session */
    f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        memset(buf, 0, sizeof(buf));
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        CHECK(n == strlen("SESSION_TOKEN_ABC="));
        CHECK(strcmp(buf, "SESSION_TOKEN_ABC=") == 0);
    }
    buf[0] = 0;
    CHECK(yt_visitor_load(buf, sizeof(buf)) == 1);
    CHECK(strcmp(buf, "SESSION_TOKEN_ABC=") == 0);

    yt_reset_visitor();
    CHECK(yt_visitor_persisted() == 0);
    CHECK(yt_visitor_load(buf, sizeof(buf)) == 0);
    remove(path);
}

static void test_duration(void)
{
    CHECK(durationToSeconds("3:45") == 225);
    CHECK(durationToSeconds("1:02:33") == 3753);
    CHECK(durationToSeconds("0:07") == 7);
    CHECK(durationToSeconds("LIVE") == 0);
    CHECK(durationToSeconds("") == 0);
}

int main(void)
{
    test_search_parse();
    test_player_parse();
    test_visitor_lifecycle();
    test_duration();
    if (failures) {
        printf("test_youtube: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_youtube: all passed\n");
    return 0;
}
