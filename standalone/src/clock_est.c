#include "clock_est.h"
#include <stdio.h>
void clock_est_format(int64_t utc_seconds,char *output,size_t capacity) {
    /* Fixed UTC-5 by owner request, including during summer. Reduce first
     * so subtracting the offset cannot overflow the input timestamp. */
    int seconds=(int)(utc_seconds%86400)-5*3600;
    if(seconds<0) seconds+=86400;
    if(seconds<0) seconds+=86400;
    int hour=seconds/3600,minute=seconds/60%60;
    snprintf(output,capacity,"%02d:%02d %s",hour%12?hour%12:12,minute,hour<12?"AM":"PM");
}
