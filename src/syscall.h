/* syscall.h - the int 0x80 syscall gate: write + exit, and the ring-3 test. */
#ifndef BASIC_SYSCALL_H
#define BASIC_SYSCALL_H

#include <stdint.h>

#define SYS_EXIT  0
#define SYS_WRITE 1

/* the stack frame the syscall stub builds (pusha regs, then the CPU frame) */
struct syscall_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;    /* pusha */
    uint32_t eip, cs, eflags;                           /* pushed by the CPU */
    uint32_t user_esp, user_ss;                         /* only on a ring change */
};

/* called from syscall_stub with the interrupted task's frame.
 * Returns 0 to return to the caller, or the next task's esp to switch
 * (only the exit path switches). */
uint32_t syscall_handler(struct syscall_frame *f);

/* jump to ring 3, write via int 0x80, then exit (spawned by the shell) */
void ring3_test(void);

#endif
