/* fs.c - in-memory filesystem (16 files, kept in RAM). */
#include <stdint.h>
#include <stddef.h>

#include "fs.h"
#include "heap.h"
#include "klib.h"
#include "vga.h"

file_t files[MAX_FILES];

file_t *fs_find(const char *name) {
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].used && !strcmp(files[i].name, name)) return &files[i];
    return NULL;
}

/* returns 0 on success, -1 bad name, -2 out of file slots, -3 out of memory */
int fs_write(const char *name, const char *text) {
    size_t nl = strlen(name);
    if (nl == 0 || nl >= NAME_LEN) return -1;

    uint32_t len = strlen(text);
    char *buf = kmalloc(len + 1);
    if (!buf) return -3;
    memcpy(buf, text, len + 1);

    file_t *f = fs_find(name);
    if (f) {
        kfree(f->data);
    } else {
        for (int i = 0; i < MAX_FILES; i++) if (!files[i].used) { f = &files[i]; break; }
        if (!f) { kfree(buf); return -2; }
        memcpy(f->name, name, nl + 1);
        f->used = 1;
    }
    f->data = buf;
    f->size = len;
    return 0;
}

int fs_remove(const char *name) {
    file_t *f = fs_find(name);
    if (!f) return -1;
    kfree(f->data);
    f->used = 0;
    return 0;
}

int fs_append(const char *name, const char *text) {
    file_t *f = fs_find(name);
    if (!f) return fs_write(name, text);
    uint32_t add = strlen(text);
    char *buf = kmalloc(f->size + add + 1);
    if (!buf) return -3;
    memcpy(buf, f->data, f->size);
    memcpy(buf + f->size, text, add + 1);
    kfree(f->data);
    f->data = buf;
    f->size += add;
    return 0;
}

int fs_copy(const char *src, const char *dst) {
    file_t *f = fs_find(src);
    if (!f) return -4;
    return fs_write(dst, f->data);
}

int fs_rename(const char *from, const char *to) {
    file_t *f = fs_find(from);
    if (!f) return -4;
    size_t n = strlen(to);
    if (n == 0 || n >= NAME_LEN) return -1;
    file_t *g = fs_find(to);
    if (g && g != f) return -5;
    memcpy(f->name, to, n + 1);
    return 0;
}

void fs_err(int r) {
    switch (r) {
    case -1: puts("bad file name (max 15 chars)\n"); break;
    case -2: puts("too many files\n"); break;
    case -3: puts("out of memory\n"); break;
    case -4: puts("no such file\n"); break;
    case -5: puts("target already exists\n"); break;
    default: puts("ok\n");
    }
}
