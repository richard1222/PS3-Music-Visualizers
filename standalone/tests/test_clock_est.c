#include "clock_est.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void check(int64_t utc,const char *expected) {
    char text[20];clock_est_format(utc,text,sizeof(text));assert(!strcmp(text,expected));
}
int main(void) {
    check(0,"07:00 PM");
    check(5*3600,"12:00 AM");
    check(17*3600,"12:00 PM");
    check(4*3600+59*60,"11:59 PM");
    check(-1,"06:59 PM");
    /* Summer and winter keep exactly the same requested UTC-5 offset. */
    check(1767268800,"07:00 AM"); /* 2026-01-01 12:00 UTC */
    check(1782907200,"07:00 AM"); /* 2026-07-01 12:00 UTC */
    char small[3];clock_est_format(0,small,sizeof(small));assert(small[2]==0);
    puts("Fixed EST clock tests passed");return 0;
}
