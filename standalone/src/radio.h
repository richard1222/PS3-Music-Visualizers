#ifndef NEON_RADIO_H
#define NEON_RADIO_H
#include <stddef.h>
#include <stdint.h>
/* Internet radio (v1.19): a curl thread fills a byte ring with the
 * station's MP3 stream; the shared minimp3 decoder reads from the
 * ring through radio_read, so EQ, spectrum and the sleep timer treat
 * radio like any other source. Status: 0 stopped, 1 connecting,
 * 2 buffering, 3 streaming (enough buffered to start decoding),
 * -1 ended or failed. */
int radio_start(const char *url,const char *ca_path);
void radio_stop(void);
int radio_status(void);
size_t radio_read(uint8_t *dst,size_t want);
size_t radio_buffered(void);
#endif
