#ifndef NL_PCM_RING_H
#define NL_PCM_RING_H
#include <stddef.h>
#include <stdint.h>
/* Caller serializes access. Wire format: 48 kHz stereo signed LE16.
 * Keep incomplete frames across HTTP chunks; discard oldest whole frames
 * when a live consumer falls behind. Maximum queued audio: 500 ms. */
#define PCM_RING_BYTES 96000
typedef struct {
    unsigned char data[PCM_RING_BYTES],partial[4];
    size_t read,count,partial_count;
    int primed;
} PcmRing;
void pcm_ring_write(PcmRing *r,const unsigned char *src,size_t bytes);
size_t pcm_ring_read(PcmRing *r,int16_t *dst,size_t samples);
#endif
