/*
 * stdio.h - Standard I/O for KikiOS programs compiled with TCC
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>

#define EOF (-1)
#define NULL ((void*)0)

/* Simple FILE type - KikiOS uses kapi directly */
typedef void FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* Basic I/O - these call into kapi */
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
int getchar(void);

/* File I/O stubs */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);

#endif /* _STDIO_H */
