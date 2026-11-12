/* paging.c - identity-maps the first 16 MB with 4 KB pages. Page 0 is left
 * unmapped, so a NULL dereference faults; kernel code and read-only data are
 * mapped read-only (CR0.WP makes that apply to the kernel itself).
 */
#include <stdint.h>

#include "paging.h"

#define PAGE_SIZE   4096
#define MAP_TABLES  4                           /* 4 x 4 MB = 16 MB */

extern char __ro_end[];                         /* from linker.ld */
extern char __user_start[], __user_end[];       /* ring-3 code + stack */

static uint32_t page_dir[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[MAP_TABLES][1024] __attribute__((aligned(4096)));

void paging_init(void) {
    uint32_t ro_end = (uint32_t)__ro_end;
    uint32_t user_start = (uint32_t)__user_start;
    uint32_t user_end = (uint32_t)__user_end;
    for (int t = 0; t < MAP_TABLES; t++) {
        int has_user = 0;
        for (int i = 0; i < 1024; i++) {
            uint32_t addr = ((uint32_t)t * 1024 + i) * PAGE_SIZE;
            uint32_t flags = 3;                 /* present + writable */
            if (addr == 0) { page_tables[t][i] = 0; continue; }
            if (addr >= 0x100000 && addr < ro_end) flags = 1;   /* present, read-only */
            if (addr >= user_start && addr < user_end) { flags |= 4; has_user = 1; }
            page_tables[t][i] = addr | flags;
        }
        /* a user-mode access faults on a supervisor page-directory entry
         * before it ever reaches the table, so a table holding user pages
         * needs the user bit here too (the table entries still decide
         * per page). */
        page_dir[t] = (uint32_t)page_tables[t] | 3 | (has_user ? 4 : 0);
    }
    __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir));
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000u;                         /* PG + WP */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
}
