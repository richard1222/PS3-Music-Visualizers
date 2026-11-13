#ifndef NL_PC_AUDIO_H
#define NL_PC_AUDIO_H
#include <stddef.h>
#include <stdint.h>
int pc_audio_start(void);
void pc_audio_stop(void);
size_t pc_audio_read(int16_t *samples,size_t count);
/* 0 idle, 1 finding PC, 2 buffering, 3 receiving, -1 disconnected/error */
int pc_audio_status(void);
#endif
