/* klib.h - the small freestanding libc: memory, strings, numbers. */
#ifndef BASIC_KLIB_H
#define BASIC_KLIB_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int v, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int atoi(const char *s);
char *trim(char *s);
void utoa(uint32_t n, char *buf);

#endif
