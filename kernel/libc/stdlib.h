/* KikiOS stdlib.h stub for TLSe */
#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

// These are provided by our kernel
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);

// Stubs
static inline int atoi(const char *s) {
    int n = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s++ - '0'); }
    return neg ? -n : n;
}

static inline long atol(const char *s) {
    return (long)atoi(s);
}

static inline void abort(void) { while (1) {} }
static inline void exit(int code) { (void)code; abort(); }

static inline void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    (void)base; (void)nmemb; (void)size; (void)compar;
}

static inline int abs(int x) { return x < 0 ? -x : x; }
static inline long labs(long x) { return x < 0 ? -x : x; }

// Basic LCG random - not secure at all
static unsigned int _stdlib_rand_seed = 1;
static inline int rand(void) {
    _stdlib_rand_seed = _stdlib_rand_seed * 1103515245 + 12345;
    return (_stdlib_rand_seed >> 16) & 0x7FFF;
}
static inline void srand(unsigned int seed) { _stdlib_rand_seed = seed; }

#define NULL ((void *)0)
#define RAND_MAX 0x7FFF

#endif
