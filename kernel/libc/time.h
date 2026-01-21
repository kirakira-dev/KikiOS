/* KikiOS time.h stub for TLSe */
#ifndef _TIME_H
#define _TIME_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t time_t;
typedef uint32_t clock_t;

#define CLOCKS_PER_SEC 1000000

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

// Will be defined in tls.c
time_t time(time_t *t);
struct tm *gmtime(const time_t *t);

// Stub
static inline clock_t clock(void) { return 0; }
static inline double difftime(time_t t1, time_t t0) { return (double)(t1 - t0); }
static inline time_t mktime(struct tm *tm) { (void)tm; return 0; }
static inline int nanosleep(const struct timespec *req, struct timespec *rem) { (void)req; (void)rem; return 0; }

#endif
