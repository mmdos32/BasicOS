/* sched.c - preemptive multitasking: tasks, sleep, timer handler, demo tasks. */
#include <stdint.h>

#include "idt.h"
#include "io.h"
#include "klib.h"
#include "panic.h"
#include "rtc.h"
#include "sched.h"
#include "vga.h"

#define STACK_SZ 4096

#define STACK_CANARY 0xDEADC0DEu
extern char stack_bottom[];                     /* the boot stack used by the shell (boot.s) */

task_t tasks[MAX_TASKS];
static uint8_t stacks[MAX_TASKS][STACK_SZ] __attribute__((aligned(16)));
volatile int cur = 0;

static void set_name(char *dst, const char *src, int max) {
    int j = 0;
    while (src[j] && j < max - 1) { dst[j] = src[j]; j++; }
    dst[j] = 0;
}

void sched_init(void) {
    *(uint32_t *)stack_bottom = STACK_CANARY;
    tasks[0].state = 1;                 /* the shell is task 0 */
    set_name(tasks[0].name, "shell", sizeof(tasks[0].name));
    tasks[0].sx = -1;
}

/* called from irq0_stub with the interrupted task's esp; returns next task's esp */
uint32_t timer_handler(uint32_t esp) {
    ticks++;
    /* a stack that grew past its end would silently corrupt its neighbour: stop instead */
    uint32_t *canary = cur == 0 ? (uint32_t *)stack_bottom : (uint32_t *)stacks[cur];
    if (tasks[cur].state != 0 && *canary != STACK_CANARY) panic("stack overflow", cur);
    tasks[cur].esp = esp;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == 2 && (int32_t)(ticks - tasks[i].wake) >= 0) tasks[i].state = 1;
    int n = cur;
    for (int k = 0; k < MAX_TASKS; k++) {
        n = (n + 1) % MAX_TASKS;
        if (tasks[n].state == 1) break;
    }
    cur = n;
    outb(0x20, 0x20);           /* EOI */
    return tasks[cur].esp;
}

static void task_exit(void) {
    tasks[cur].state = 0;
    for (;;) cpu_halt();
}

/* terminate the calling task (used by the exit syscall) and switch to the
 * next ready one; returns the next task's esp, or 0 if none is ready. */
uint32_t task_exit_now(void) {
    tasks[cur].state = 0;
    int n = cur;
    for (int k = 0; k < MAX_TASKS; k++) {
        n = (n + 1) % MAX_TASKS;
        if (tasks[n].state == 1) break;
    }
    if (tasks[n].state != 1) return 0;
    cur = n;
    return tasks[cur].esp;
}

static void task_main(void) {
    tasks[cur].entry();
    task_exit();
}

int task_spawn(void (*fn)(void), const char *name) {
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state != 0) continue;
        uint32_t *sp = (uint32_t *)(stacks[i] + STACK_SZ);
        *--sp = 0x202;                      /* eflags (IF set) */
        *--sp = 0x08;                       /* cs */
        *--sp = (uint32_t)task_main;        /* eip */
        for (int r = 0; r < 8; r++) *--sp = 0;  /* pusha frame */
        *(uint32_t *)stacks[i] = STACK_CANARY;
        tasks[i].esp = (uint32_t)sp;
        tasks[i].entry = fn;
        set_name(tasks[i].name, name, sizeof(tasks[i].name));
        tasks[i].sx = -1;
        tasks[i].sw = 0;
        tasks[i].state = 1;
        return i;
    }
    return -1;
}

/* block the calling task for at least `ms` milliseconds; other tasks keep running */
void task_sleep(uint32_t ms) {
    int me = cur;
    uint32_t t = ms * TIMER_HZ / 1000;
    if (t == 0) t = 1;
    tasks[me].wake = ticks + t;
    tasks[me].state = 2;
    while (tasks[me].state == 2) cpu_halt();
}

/* demo worker: shows a per-task counter in the status bar (row 0) */
void worker(void) {
    int id = cur;
    int x = 10 + (id - 1) * 9;
    tasks[id].sx = x; tasks[id].sw = 8;
    uint32_t n = 0, last = ticks;
    for (;;) {
        if (ticks != last) {
            last = ticks; n++;
            char buf[12]; utoa(n, buf);
            vga_at(x, 0, 'T', 0x1E);
            vga_at(x + 1, 0, '0' + id, 0x1E);
            vga_at(x + 2, 0, ':', 0x1E);
            int k = 0;
            for (; buf[k] && k < 5; k++) vga_at(x + 3 + k, 0, buf[k], 0x1E);
            for (; k < 5; k++) vga_at(x + 3 + k, 0, ' ', 0x1E);
        }
        cpu_halt();
    }
}

/* clock: shows HH:MM:SS at the right end of the status bar */
void clock_task(void) {
    tasks[cur].sx = 72; tasks[cur].sw = 8;
    uint32_t last = (uint32_t)-1;
    for (;;) {
        uint32_t sec = ticks / TIMER_HZ;
        if (sec != last) {
            last = sec;
            rtc_t t; rtc_read(&t);
            char s[8] = { '0' + t.hour / 10, '0' + t.hour % 10, ':',
                          '0' + t.min / 10,  '0' + t.min % 10,  ':',
                          '0' + t.sec / 10,  '0' + t.sec % 10 };
            for (int k = 0; k < 8; k++) vga_at(72 + k, 0, s[k], 0x1F);
        }
        task_sleep(250);
    }
}
