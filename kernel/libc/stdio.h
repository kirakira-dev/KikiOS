/* KikiOS stdio.h stub for TLSe */
#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef void FILE;
#define stderr ((FILE*)0)
#define stdout ((FILE*)0)
#define stdin  ((FILE*)0)

#define EOF (-1)
#define _IONBF 2
#define _IOLBF 1
#define _IOFBF 0

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// UART output for DEBUG
extern void uart_puts(const char *s);

// Stubs
static inline int setvbuf(FILE *f, char *buf, int mode, size_t size) { (void)f; (void)buf; (void)mode; (void)size; return 0; }
static inline int fprintf(FILE *f, const char *fmt, ...) {
    (void)f;
    uart_puts(fmt);  // Output format string to UART (no formatting, but shows which debug line)
    return 0;
}
static inline int printf(const char *fmt, ...) { (void)fmt; return 0; }
static inline int sprintf(char *buf, const char *fmt, ...) { (void)fmt; buf[0] = '\0'; return 0; }
static inline int snprintf(char *buf, size_t size, const char *fmt, ...) { (void)fmt; if (size > 0) buf[0] = '\0'; return 0; }
static inline int fflush(FILE *f) { (void)f; return 0; }
static inline size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) { (void)ptr; (void)stream; return size * nmemb; }
static inline size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) { (void)ptr; (void)stream; (void)size; (void)nmemb; return 0; }
static inline FILE *fopen(const char *path, const char *mode) { (void)path; (void)mode; return 0; }
static inline int fclose(FILE *f) { (void)f; return 0; }
static inline void perror(const char *s) { (void)s; }
static inline int fgetc(FILE *f) { (void)f; return EOF; }
static inline int fputc(int c, FILE *f) { (void)c; (void)f; return c; }
static inline int fputs(const char *s, FILE *f) { (void)s; (void)f; return 0; }
static inline int getc(FILE *f) { (void)f; return EOF; }
static inline int putc(int c, FILE *f) { (void)c; (void)f; return c; }

#endif
