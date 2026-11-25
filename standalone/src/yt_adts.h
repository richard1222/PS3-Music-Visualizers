/* yt_adts.h - the ADTS header writer (pure, host-tested).
 *
 * YouTube's MP4 audio track stores raw AAC frames; FAAD2 (like
 * mohasi's cellAdec wrapper, whose buildAdtsHeader this follows -
 * ps3-dev, Apache-2.0; see NOTICE) syncs on ADTS, so each frame is
 * wrapped in a 7-byte header carrying the CORE sample rate: for
 * HE-AAC the frames are the half-rate LC core and the SBR layer is
 * in-band, so the header names the core rate, not the output rate. */
#ifndef NL_YT_ADTS_H
#define NL_YT_ADTS_H

#include <stdint.h>

#define YT_ADTS_HEADER_BYTES 7

/* Writes the header for one raw AAC frame of payload_bytes at the
 * given core rate / channel count. 0 ok, -1 when the rate or channel
 * count cannot be expressed in ADTS. */
int yt_adts_header(uint8_t out[YT_ADTS_HEADER_BYTES], int payload_bytes, int rate, int channels);

#endif
