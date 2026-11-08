/* fs.h - in-memory filesystem (16 files, kept in RAM). */
#ifndef BASIC_FS_H
#define BASIC_FS_H

#include <stdint.h>

#define MAX_FILES 16
#define NAME_LEN  16

typedef struct {
    char name[NAME_LEN];
    char *data;
    uint32_t size;
    int used;
} file_t;

extern file_t files[MAX_FILES];         /* the file table (ls walks it directly) */

file_t *fs_find(const char *name);

/* returns 0 on success, -1 bad name, -2 out of file slots, -3 out of memory,
 * -4 no such file, -5 target already exists */
int fs_write(const char *name, const char *text);
int fs_remove(const char *name);
int fs_append(const char *name, const char *text);
int fs_copy(const char *src, const char *dst);
int fs_rename(const char *from, const char *to);

void fs_err(int r);

#endif
