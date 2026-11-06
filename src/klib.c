/* klib.c - the small freestanding libc: memory, strings, numbers. */
#include "klib.h"

void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dp = d; const uint8_t *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memset(void *d, int v, size_t n) {
    uint8_t *dp = d;
    while (n--) *dp++ = (uint8_t)v;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    uint8_t *dp = d; const uint8_t *sp = s;
    if (dp < sp) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

int atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        if (n > 100000000) return 100000000;    /* clamp instead of overflowing */
        n = n * 10 + (*s++ - '0');
    }
    return n;
}

/* strips leading and trailing spaces in place; returns the new start of the string */
char *trim(char *s) {
    while (*s == ' ') s++;
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
    return s;
}

void utoa(uint32_t n, char *buf) {
    char tmp[12]; int i = 0, j = 0;
    if (n == 0) tmp[i++] = '0';
    while (n) { tmp[i++] = '0' + n % 10; n /= 10; }
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
}
