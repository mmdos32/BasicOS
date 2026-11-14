/* tss.c - Task State Segment setup. */
#include "tss.h"

#include "klib.h"

/* The ring-0 stack the CPU switches to on a ring-3 -> ring-0 transition.
 * It must NOT be the boot stack: the shell (task 0) runs on the boot
 * stack, so a blocking syscall (sleep) would let the timer IRQ frames
 * land on the shell's saved frame and corrupt its return - that halted
 * v0.13's first sleep test with a page fault at the timer iret. One
 * ring-3 test at a time, as with the shared user stack. */
static uint8_t syscall_stack[4096] __attribute__((aligned(16)));

struct tss_entry tss;

void tss_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = 0x10;                 /* kernel data segment */
    tss.esp0 = (uint32_t)(syscall_stack + sizeof(syscall_stack));
    __asm__ volatile("ltr %%ax" :: "a"((uint16_t)TSS_SELECTOR));
}
