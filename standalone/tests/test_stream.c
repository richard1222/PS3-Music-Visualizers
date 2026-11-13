#include "pcm_ring.h"
#include "frame_health.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static PcmRing ring;
int main(void) {
    unsigned char data[PCM_RING_BYTES+400]; int16_t out[4096];
    for(size_t i=0;i<sizeof(data);i+=4) {data[i]=0x34;data[i+1]=0x12;data[i+2]=0;data[i+3]=0x80;}
    pcm_ring_write(&ring,data,1);pcm_ring_write(&ring,data+1,2);
    assert(ring.count==0 && ring.partial_count==3);
    pcm_ring_write(&ring,data+3,28801); assert(ring.count==28804);
    assert(pcm_ring_read(&ring,out,4096)==4096);
    assert(out[0]==0x1234 && out[1]==-32768);
    memset(&ring,0,sizeof(ring)); pcm_ring_write(&ring,data,sizeof(data));
    assert(ring.count==PCM_RING_BYTES && ring.read==400);
    for(int i=0;i<12;i++) pcm_ring_read(&ring,out,4096);
    assert(!ring.count && !ring.primed);
    assert(pcm_ring_read(&ring,out,4096)==0 && out[0]==0);
    FrameHealth h={0};
    for(int i=0;i<2000;i++) assert(!frame_health_update(&h,17));
    for(int i=0;i<99;i++) assert(!frame_health_update(&h,100));
    assert(!frame_health_update(&h,16) && !h.slow_ms);
    assert(frame_health_update(&h,10000));
    puts("PCM fragmentation, endian conversion, wrap, overflow, starvation and frame safety passed");
}
