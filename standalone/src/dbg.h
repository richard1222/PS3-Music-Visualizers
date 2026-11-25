/* dbg.h - Neon Lounge's stand-in for mohasi's simple-lib-core dbg.h.
 *
 * The ported demuxer files (demux-mp4.c, h264.c) log through
 * logInfo/logWarn/logError/logTrace. Upstream's dbg.h writes to a
 * debug file through their VFS + RTC stack, which Neon Lounge does
 * not have; this shim keeps the same call surface and routes the
 * lines into the app's YouTube log (yt_log_sink), so demuxer
 * diagnostics land beside the extractor's. The ported files are
 * otherwise unmodified. */
#pragma once

#include <stdarg.h>

/* Shared sink: formats one line and hands it to yt_log_sink. */
void yt_demux_log(const char *level, const char *fmt, va_list ap);

/* Lines nobody wants by default (kept for source compatibility). */
extern int logDetailed;

static inline void logInfo(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static inline void logWarn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static inline void logError(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static inline void logTrace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static inline void logInfo(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); yt_demux_log("info", fmt, ap); va_end(ap);
}

static inline void logWarn(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); yt_demux_log("warn", fmt, ap); va_end(ap);
}

static inline void logError(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); yt_demux_log("error", fmt, ap); va_end(ap);
}

static inline void logTrace(const char *fmt, ...)
{
    if (!logDetailed) return;
    va_list ap; va_start(ap, fmt); yt_demux_log("trace", fmt, ap); va_end(ap);
}
