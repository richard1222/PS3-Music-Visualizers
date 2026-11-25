/* yt_net.c - libcurl transport for the YouTube extractor (see yt_net.h),
 * plus the extractor's log sink.
 *
 * Freeze-rule notes: every transfer has a total timeout, a connect
 * timeout and a low-speed abort, mirroring the update checker; a
 * progress callback aborts the transfer as soon as yt_net_cancel() is
 * set, so stopping the worker can never wait on a stuck socket. */
#include "yt_net.h"
#include "youtube.h"
#include <stdio.h>
#include <string.h>

/* ---- log sink ---------------------------------------------------------- */

static int log_lines;

void yt_log_sink(const char *level, const char *line)
{
    if (log_lines >= 4000) return;
    log_lines++;
#ifdef NEON_PS3
    char path[320];
    snprintf(path, sizeof(path), "%s/youtube.log", yt_data_dir());
    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "[%s] %s", level, line);
        if (line[0] && line[strlen(line) - 1] != '\n') fputc('\n', f);
        fclose(f);
    }
#else
    fprintf(stderr, "[yt %s] %s", level, line);
    if (line[0] && line[strlen(line) - 1] != '\n') fputc('\n', stderr);
#endif
    (void)level;
}

/* ---- transport --------------------------------------------------------- */

static char ca_path[768] = "";
static volatile int cancel_flag;

void yt_net_set_ca(const char *path)
{
    if (path) snprintf(ca_path, sizeof(ca_path), "%s", path);
    else ca_path[0] = 0;
}

const char *yt_net_ca_path(void)
{
    return ca_path;
}

void yt_net_cancel(int on) { cancel_flag = on; }

#ifdef NEON_PS3

#include "update_tls.h"
#include <curl/curl.h>
#include <stdlib.h>

typedef struct {
    char *buf;
    size_t len, cap;
    HttpHeaderCapture *capture;
} FetchBuf;

static size_t fetch_write(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    FetchBuf *fb = (FetchBuf *)userdata;
    size_t n = size * nmemb;
    if (cancel_flag) return 0;
    /* store what fits and consume the rest: a truncated body is a normal
     * outcome for the extractor (RESP_CAP), not a transfer error */
    size_t room = (fb->cap - 1 > fb->len) ? (fb->cap - 1 - fb->len) : 0;
    size_t take = n < room ? n : room;
    if (take) {
        memcpy(fb->buf + fb->len, ptr, take);
        fb->len += take;
        fb->buf[fb->len] = 0;
    }
    return n;
}

static size_t fetch_header(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    FetchBuf *fb = (FetchBuf *)userdata;
    size_t n = size * nmemb;
    if (fb->capture && fb->capture->name && fb->capture->value && fb->capture->cap > 0) {
        const char *line = (const char *)ptr;
        size_t name_len = strlen(fb->capture->name);
        size_t i = 0;
        int match = n > name_len && line[name_len] == ':';
        for (i = 0; match && i < name_len; i++) {
            char a = line[i], b = fb->capture->name[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) match = 0;
        }
        if (match) {
            const char *v = line + name_len + 1;
            const char *end = line + n;
            while (v < end && (*v == ' ' || *v == '\t')) v++;
            while (end > v && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ')) end--;
            size_t out = 0;
            while (v < end && out < (size_t)fb->capture->cap - 1) fb->capture->value[out++] = *v++;
            fb->capture->value[out] = 0;
        }
    }
    return n;
}

static int fetch_progress(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)clientp; (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return cancel_flag ? 1 : 0;
}

int fetchHttpCapturing(const char *method, const char *url, const HttpHeader *headers, int headerCount,
                       const void *body, int bodyLen, char *out, int cap, int *outLen, int *status,
                       HttpHeaderCapture *capture)
{
    if (outLen) *outLen = 0;
    if (status) *status = 0;
    if (capture && capture->value && capture->cap > 0) capture->value[0] = 0;
    if (!url || !out || cap < 2) return -1;
    out[0] = 0;
    if (!ca_path[0]) return -1;   /* no CA bundle configured: refuse rather than go unverified */

    CURL *c = curl_easy_init();
    if (!c) return -1;
    FetchBuf fb;
    fb.buf = out; fb.len = 0; fb.cap = (size_t)cap; fb.capture = capture;
    struct curl_slist *list = NULL;
    for (int i = 0; i < headerCount; i++) {
        char line[4096];
        if (snprintf(line, sizeof(line), "%s: %s", headers[i].name, headers[i].value) < (int)sizeof(line))
            list = curl_slist_append(list, line);
    }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fetch_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &fb);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, fetch_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &fb);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, fetch_progress);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    if (update_configure_tls(c, ca_path) != CURLE_OK) {
        if (list) curl_slist_free_all(list);
        curl_easy_cleanup(c);
        return -1;
    }
    if (method && strcmp(method, "GET") != 0) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body ? (const char *)body : "");
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)(body ? bodyLen : 0));
        if (strcmp(method, "POST") != 0) curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    }
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (list) curl_slist_free_all(list);
    curl_easy_cleanup(c);
    if (status) *status = (int)code;
    if (outLen) *outLen = (int)fb.len;
    if (rc == CURLE_ABORTED_BY_CALLBACK) return -3;
    if (rc != CURLE_OK) return -1;
    return 0;
}

int fetchHttp(const char *method, const char *url, const HttpHeader *headers, int headerCount,
              const void *body, int bodyLen, char *out, int cap, int *outLen, int *status)
{
    return fetchHttpCapturing(method, url, headers, headerCount, body, bodyLen, out, cap, outLen, status, NULL);
}

#else  /* host build: no network, so the extractor's fetches fail cleanly */

int fetchHttpCapturing(const char *method, const char *url, const HttpHeader *headers, int headerCount,
                       const void *body, int bodyLen, char *out, int cap, int *outLen, int *status,
                       HttpHeaderCapture *capture)
{
    (void)method; (void)url; (void)headers; (void)headerCount; (void)body; (void)bodyLen;
    if (out && cap > 0) out[0] = 0;
    if (outLen) *outLen = 0;
    if (status) *status = 0;
    if (capture && capture->value && capture->cap > 0) capture->value[0] = 0;
    return -1;
}

int fetchHttp(const char *method, const char *url, const HttpHeader *headers, int headerCount,
              const void *body, int bodyLen, char *out, int cap, int *outLen, int *status)
{
    return fetchHttpCapturing(method, url, headers, headerCount, body, bodyLen, out, cap, outLen, status, NULL);
}

#endif
