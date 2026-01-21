/*
 * KikiOS Printf
 */

#ifndef PRINTF_H
#define PRINTF_H

#include <stdarg.h>

// Printf to UART console
int printf(const char *fmt, ...);

// Sprintf to buffer
int sprintf(char *buf, const char *fmt, ...);

// Snprintf with length limit
int snprintf(char *buf, int size, const char *fmt, ...);

#endif
