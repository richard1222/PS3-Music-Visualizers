/* yt_demux.c - see yt_demux.h. buildVideoAu below is verbatim from
 * upstream's demux.c (the shared Annex-B access-unit builder the MP4
 * demuxer calls); the open helpers mirror upstream demux.c's MP4
 * branches of openVideoDemuxer/openAudioDemuxer, minus the MKV half.
 * Also home to the dbg.h shim's sink (yt_demux_log + logDetailed). */
#include "yt_demux.h"
#include "video-source.h"
#include "youtube.h"     /* yt_log_sink */
#include <stdio.h>
#include <string.h>

/* ---- dbg shim sink ---------------------------------------------------- */

int logDetailed = 0;

void yt_demux_log(const char *level, const char *fmt, va_list ap)
{
    char line[512];
    vsnprintf(line, sizeof(line), fmt, ap);
    yt_log_sink(level, line);
}

/* ---- the shared AU builder (upstream demux.c, verbatim) --------------- */

// isSyncSample: the container marked this sample a random-access point (mp4 sync sample / trun sync flag).
// A fragment/segment can open on a non-IDR recovery-point I-frame, which carries no type-5 NAL; treat that
// like an IDR so a freshly reset decoder gets its SPS/PPS and the post-seek decode gate starts on it.
int buildVideoAu(const H264Config *h264, const uint8_t *sample, int sampleSize, int isSyncSample, uint8_t *auBuffer, int auCapacity, uint64_t pts, VideoAu *au)
{
   int reserve = h264->headerSize;   // room to prepend SPS/PPS ahead of a random-access frame
   int isIdr = 0;
   int annexLen = avccToAnnexB(sample, sampleSize, h264->nalLengthSize, auBuffer + reserve, auCapacity - reserve, &isIdr);
   if (annexLen <= 0) return 0;
   int isKeyframe = isIdr || isSyncSample;

   au->pts = pts;
   if (isKeyframe && reserve > 0) {
      memcpy(auBuffer, h264->header, reserve);
      au->data = auBuffer;
      au->size = reserve + annexLen;
      au->keyframe = 1;
   } else {
      au->data = auBuffer + reserve;
      au->size = annexLen;
      au->keyframe = isKeyframe;
   }
   return 1;
}

/* ---- opens -------------------------------------------------------------- */

static int openSniffed(Mp4Demuxer *demuxer, const char *path, volatile int *cancelFlag, int audioOnly)
{
   VideoSource source;
   memset(&source, 0, sizeof(source));
   videoSourceSetCancel(&source, cancelFlag);
   if (openVideoSource(&source, path) != 0) {
      yt_log_sink("error", "[demux] could not open source");
      return -1;
   }
   if (!audioOnly) {
      /* the container sniff from upstream's openVideoDemuxer: MP4 has
       * 'ftyp' at offset 4. Anything else is not a YouTube format we
       * can play (MKV is not ported). */
      uint8_t magic[8] = {0};
      readVideoSourceAt(&source, 0, magic, sizeof magic);
      if (!(magic[4] == 'f' && magic[5] == 't' && magic[6] == 'y' && magic[7] == 'p')) {
         yt_log_sink("error", "[demux] not an MP4 stream");
         closeVideoSource(&source);
         return -1;
      }
   }
   if (openMp4Demuxer(demuxer, &source, audioOnly) != 0) return -1;   /* adopts (and on failure closes) the source */
   return 0;
}

int yt_demux_open_video(Mp4Demuxer *demuxer, const char *path, volatile int *cancelFlag)
{
   memset(demuxer, 0, sizeof *demuxer);
   if (openSniffed(demuxer, path, cancelFlag, 0) != 0) return -1;
   if (!demuxer->h264.valid) { closeMp4Demuxer(demuxer); return -1; }
   return 0;
}

int yt_demux_open_audio(Mp4Demuxer *demuxer, const char *path, volatile int *cancelFlag)
{
   memset(demuxer, 0, sizeof *demuxer);
   if (openSniffed(demuxer, path, cancelFlag, 1) != 0) return -1;
   if (!demuxer->hasAudio) { closeMp4Demuxer(demuxer); return -1; }
   return 0;
}
