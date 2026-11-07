/* heap.c - first-fit free list over a static arena. Only the shell (task 0)
 * allocates, so no locking is needed.
 */
#include <stddef.h>
#include <stdint.h>

#include "heap.h"

#define HEAP_SIZE (256 * 1024)

typedef struct block {
    uint32_t size;
    uint32_t free;
    struct block *next;
    uint32_t pad;               /* keeps the header at 16 bytes */
} block_t;

static uint8_t heap[HEAP_SIZE] __attribute__((aligned(16)));
static block_t *heap_head;

void heap_init(void) {
    heap_head = (block_t *)heap;
    heap_head->size = HEAP_SIZE - sizeof(block_t);
    heap_head->free = 1;
    heap_head->next = NULL;
}

void *kmalloc(uint32_t n) {
    if (n > HEAP_SIZE) return NULL;     /* also keeps the rounding below from wrapping */
    n = (n + 15) & ~15u;
    if (n == 0) n = 16;
    for (block_t *b = heap_head; b; b = b->next) {
        if (!b->free || b->size < n) continue;
        if (b->size >= n + sizeof(block_t) + 16) {      /* split */
            block_t *s = (block_t *)((uint8_t *)(b + 1) + n);
            s->size = b->size - n - sizeof(block_t);
            s->free = 1;
            s->next = b->next;
            b->next = s;
            b->size = n;
        }
        b->free = 0;
        return b + 1;
    }
    return NULL;
}

void kfree(void *p) {
    if (!p) return;
    uint8_t *q = p;
    if (q < heap + sizeof(block_t) || q >= heap + HEAP_SIZE) return;    /* not a heap pointer */
    block_t *b = (block_t *)p - 1;
    if (b->free) return;                                                /* double free */
    b->free = 1;
    for (block_t *c = heap_head; c; c = c->next) {      /* coalesce */
        while (c->free && c->next && c->next->free) {
            c->size += sizeof(block_t) + c->next->size;
            c->next = c->next->next;
        }
    }
}

void heap_stats(uint32_t *used, uint32_t *freeb) {
    *used = 0; *freeb = 0;
    for (block_t *b = heap_head; b; b = b->next) {
        if (b->free) *freeb += b->size; else *used += b->size;
    }
}
