/* yt_adts.c - see yt_adts.h. The bit layout matches mohasi's
 * buildAdtsHeader (ps3-dev libs/simple-lib-av, Apache-2.0). */
#include "yt_adts.h"

int yt_adts_header(uint8_t out[YT_ADTS_HEADER_BYTES], int payload_bytes, int rate, int channels)
{
    static const int rates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                 22050, 16000, 12000, 11025, 8000, 7350 };
    int freq_index = -1;
    for (int i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++)
        if (rates[i] == rate) { freq_index = i; break; }
    if (freq_index < 0 || channels < 1 || channels > 6 || payload_bytes <= 0) return -1;

    int frame_length = payload_bytes + YT_ADTS_HEADER_BYTES;
    out[0] = 0xFF;                                                         /* syncword */
    out[1] = 0xF1;                                                         /* syncword, MPEG-4, no CRC */
    out[2] = (uint8_t)(0x40 | (freq_index << 2) | (channels >> 2));        /* AAC-LC, freq, chan hi */
    out[3] = (uint8_t)(((channels & 3) << 6) | ((frame_length >> 11) & 3));
    out[4] = (uint8_t)(frame_length >> 3);
    out[5] = (uint8_t)(((frame_length & 7) << 5) | 0x1F);                  /* fullness: VBR */
    out[6] = 0xFC;
    return 0;
}
