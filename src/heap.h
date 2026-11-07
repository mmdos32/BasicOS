/* heap.h - first-fit kernel heap allocator over a static arena. */
#ifndef BASIC_HEAP_H
#define BASIC_HEAP_H

#include <stdint.h>

void heap_init(void);
void *kmalloc(uint32_t n);
void kfree(void *p);
void heap_stats(uint32_t *used, uint32_t *freeb);

#endif
