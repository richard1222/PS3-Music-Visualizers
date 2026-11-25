/* video-source.h - the seekable reader the ported MP4 demuxer reads
 * through. Interface and read-ahead design from mohasi's ps3-dev
 * simple-lib-av (Apache-2.0; see NOTICE), retargeted for Neon Lounge:
 * upstream's backends are their VFS and their BearSSL/cellHttp HTTP
 * stream; here a source is either a local stdio file or an HTTP(S)
 * stream served by a curl-backed prefetch ring (see video-source.c),
 * because googlevideo's 2026 rules (byte ranges in the URL query, a
 * small per-address request budget) shape how the bytes may be
 * fetched. The demuxer above this header is unmodified. */
#pragma once

#include <stdint.h>
#include <stdio.h>

typedef struct {
   int         isOpen;
   int         isHttp;       /* streamed url (curl ring) vs local file */
   FILE       *file;         /* valid when !isHttp */
   void       *http;         /* YtHttpBackend*, valid when isHttp (PS3 only) */
   uint64_t    size;         /* total size in bytes (0 = unknown) */
   uint64_t    pos;          /* logical read position the caller sees */
   uint64_t    filePos;      /* actual backend position */
   uint8_t    *buffer;       /* read-ahead buffer (heap; freed on close) */
   int         bufferLen;    /* valid bytes currently in the buffer */
   uint64_t    bufferStart;  /* offset the buffer begins at */
   volatile int *cancelFlag; /* owner-provided: aborts http waits when set */
} VideoSource;

/* Attach the owner's cancel flag before opening an http source; a
 * blocked reader then fails fast when the owner stops the stream. */
void     videoSourceSetCancel(VideoSource *source, volatile int *flag);

int      openVideoSource(VideoSource *source, const char *path);              /* 0 / -1 */
int64_t  readVideoSource(VideoSource *source, void *buffer, uint64_t length); /* bytes read, or -1 */
int      seekVideoSource(VideoSource *source, uint64_t offset);               /* absolute seek; 0 / -1 */
uint64_t getVideoSourcePosition(const VideoSource *source);
uint64_t getVideoSourceSize(const VideoSource *source);
/* Note: getVideoSourcePosition() is the reader's logical (parse)
 * position - read-ahead lives in the buffer ahead of it - so it is
 * exactly where a replacement source resumes after a re-extract. */
void     cancelVideoSource(VideoSource *source);   /* unblock a waiting http read (before teardown) */
void     closeVideoSource(VideoSource *source);

/* Why an http source failed: 0 = no error, 1 = the address was refused
 * (googlevideo's per-address budget is spent; a fresh extract is the
 * only cure), 2 = any other transport failure. File sources: 0. */
int      videoSourceError(const VideoSource *source);

/* read exactly `length` bytes at absolute `offset` (seek + full read).
 * returns 0 on success, -1 on any short read or error. */
int      readVideoSourceAt(VideoSource *source, uint64_t offset, void *buffer, uint64_t length);
