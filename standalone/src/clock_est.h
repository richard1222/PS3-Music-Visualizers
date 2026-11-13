#ifndef NL_CLOCK_EST_H
#define NL_CLOCK_EST_H
#include <stdint.h>
#include <stddef.h>
void clock_est_format(int64_t utc_seconds,char *output,size_t capacity);
#endif
