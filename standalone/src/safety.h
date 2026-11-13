#ifndef NL_SAFETY_H
#define NL_SAFETY_H
int safety_start(void);
void safety_beat(void);
void safety_allow_seconds(int seconds);
void safety_stop(void);
void safety_stage(int stage);
#endif
