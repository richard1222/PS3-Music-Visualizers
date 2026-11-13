#include "pcm_ring.h"
#include <string.h>
void pcm_ring_write(PcmRing *r,const unsigned char *src,size_t bytes) {
    while(bytes--) {
        r->partial[r->partial_count++]=*src++;
        if(r->partial_count!=4) continue;
        if(r->count==PCM_RING_BYTES) { r->read=(r->read+4)%PCM_RING_BYTES; r->count-=4; }
        size_t end=(r->read+r->count)%PCM_RING_BYTES;
        memcpy(r->data+end,r->partial,4);
        r->count+=4; r->partial_count=0;
    }
}
size_t pcm_ring_read(PcmRing *r,int16_t *dst,size_t samples) {
    memset(dst,0,samples*sizeof(*dst));
    /* Rebuffer 150 ms after starvation rather than output tiny bursts. */
    if(!r->primed && r->count<28800) return 0;
    r->primed=1;
    size_t frames=samples/2;
    if(frames>r->count/4) frames=r->count/4;
    for(size_t i=0;i<frames*2;i++) {
        unsigned lo=r->data[r->read],hi=r->data[(r->read+1)%PCM_RING_BYTES];
        dst[i]=(int16_t)((hi<<8)|lo);
        r->read=(r->read+2)%PCM_RING_BYTES; r->count-=2;
    }
    if(frames<samples/2) r->primed=0;
    return frames*2;
}
