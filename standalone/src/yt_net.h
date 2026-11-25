/* yt_net.h - the one HTTP(S) transport the YouTube extractor runs over.
 *
 * The fetchHttp/fetchHttpCapturing shape is mohasi's simple-lib-core
 * http.h (Apache-2.0; see NOTICE). The implementation in yt_net.c is
 * Neon Lounge's own: libcurl + mbedTLS with the same CA bundle and
 * HTTPS-only policy as the update checker (update_tls.h). */
#ifndef NL_YT_NET_H
#define NL_YT_NET_H

typedef struct { const char *name; const char *value; } HttpHeader;

/* Perform one request and write the response BODY (NUL-terminated,
 * truncated to cap-1) into out. Returns 0 when a response arrived (HTTP
 * status in *status, stored body length in *outLen), negative on a
 * connect/transport/TLS failure or when cancelled via yt_net_cancel().
 * A non-2xx status is still a 0 return - inspect *status. */
int fetchHttp(const char *method, const char *url, const HttpHeader *headers, int headerCount,
              const void *body, int bodyLen, char *out, int cap, int *outLen, int *status);

/* One response header to copy out of the reply, e.g. { "x-head-seqnum", buf, cap }.
 * The name match is case-insensitive; value is emptied when absent. */
typedef struct { const char *name; char *value; int cap; } HttpHeaderCapture;

/* As fetchHttp, plus one captured response header. */
int fetchHttpCapturing(const char *method, const char *url, const HttpHeader *headers, int headerCount,
                       const void *body, int bodyLen, char *out, int cap, int *outLen, int *status,
                       HttpHeaderCapture *capture);

/* CA bundle for TLS verification (the app passes USRDIR/certs/cacert.pem). */
void yt_net_set_ca(const char *path);

/* The configured CA bundle path ("" when unset). The video source's
 * streaming transport verifies against the same bundle. */
const char *yt_net_ca_path(void);

/* Ask any in-flight fetch to abort at the next progress tick. The worker
 * sets this when it is being stopped or a job is superseded. */
void yt_net_cancel(int on);

#endif
