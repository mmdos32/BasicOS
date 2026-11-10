/* tss.c - Task State Segment setup. */
#include "tss.h"

#include "klib.h"

extern uint32_t stack_top;          /* top of the kernel stack (boot.s) */

struct tss_entry tss;

void tss_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = 0x10;                 /* kernel data segment */
    tss.esp0 = (uint32_t)&stack_top;
    __asm__ volatile("ltr %%ax" :: "a"((uint16_t)TSS_SELECTOR));
}
