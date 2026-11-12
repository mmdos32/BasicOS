/* syscall.c - the int 0x80 gate: write (fd 1 only) + exit, and the ring-3 test. */
#include <stdint.h>

#include "io.h"
#include "sched.h"
#include "syscall.h"
#include "vga.h"

#define USER_MEM_TOP 0x1000000u    /* paging maps the first 16 MB identity */
#define MAX_WRITE 4096

uint32_t syscall_handler(struct syscall_frame *f) {
    if (f->eax == SYS_WRITE) {
        uint32_t fd = f->ebx, len = f->edx;
        const char *buf = (const char *)f->ecx;
        if (fd != 1 || !buf || len > MAX_WRITE || (uint32_t)buf > USER_MEM_TOP - len) {
            f->eax = (uint32_t)-1;
            return 0;
        }
        for (uint32_t i = 0; i < len; i++) putc(buf[i]);
        f->eax = len;
        return 0;
    }
    if (f->eax == SYS_EXIT) {
        uint32_t next = task_exit_now();
        if (!next) {
            puts("no tasks left, halting\n");
            for (;;) cpu_halt();
        }
        return next;
    }
    f->eax = (uint32_t)-1;      /* unknown syscall number */
    return 0;
}

/* ring-3 code and stack live in the .user section (see linker.ld): the
 * kernel .text is mapped supervisor-only, so ring 3 could neither fetch
 * code nor push a stack there. The stack is shared, so a second test
 * while one is running is refused. */
__attribute__((section(".user_text")))
static void ring3_entry(void) {
    static const char msg[] = "hello from ring 3\n";
    uint32_t ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"((uint32_t)SYS_WRITE), "b"(1), "c"(msg), "d"(sizeof(msg) - 1)
                     : "memory");
    (void)ret;
    __asm__ volatile("int $0x80" :: "a"((uint32_t)SYS_EXIT), "b"(0) : "memory");
    for (;;) cpu_halt();
}

static uint8_t user_stack[2048] __attribute__((section(".user_bss"), aligned(16)));

void ring3_test(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (i != cur && tasks[i].state && tasks[i].entry == ring3_test) {
            puts("ring3 test already running\n");
            return;
        }
    }
    uint32_t *sp = (uint32_t *)(user_stack + sizeof(user_stack));
    puts("ring3 test: jumping to ring 3...\n");
    *--sp = 0x23;                                /* ss (user data, RPL 3) */
    *--sp = (uint32_t)(user_stack + sizeof(user_stack)); /* esp */
    *--sp = 0x202;                               /* eflags (IF set) */
    *--sp = 0x1B;                                /* cs (user code, RPL 3) */
    *--sp = (uint32_t)ring3_entry;               /* eip */
    __asm__ volatile("mov %0, %%esp\n\tiret" :: "r"(sp) : "memory");
    __builtin_unreachable();
}
