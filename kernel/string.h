/*
 * KikiOS String Functions
 */

#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>

// Memory operations
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memset32(void *s, uint32_t val, size_t count);  // Fill with 32-bit pattern
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

// String operations
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *dest, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strdup(const char *s);

// Case-insensitive compare (for our case-insensitive filesystem)
int strcasecmp(const char *s1, const char *s2);

// Tokenization
char *strtok_r(char *str, const char *delim, char **saveptr);

// Search
char *strstr(const char *haystack, const char *needle);
void *memchr(const void *s, int c, size_t n);

#endif
