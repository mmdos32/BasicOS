/* tss.h - Task State Segment (32-bit, 104 bytes).
 * Holds the ring-0 stack the CPU switches to on a ring-3 -> ring-0
 * transition. Only ss0/esp0 are used for now; the rest is zeroed.
 * Loaded with ltr during init; nothing runs in ring 3 yet.
 */
#ifndef BASIC_TSS_H
#define BASIC_TSS_H

#include <stdint.h>

#define TSS_SELECTOR 0x28           /* GDT entry 5 */

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3;
    uint32_t eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

extern struct tss_entry tss;

void tss_init(void);

#endif
