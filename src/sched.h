/* sched.h - preemptive multitasking: tasks, sleep, timer handler, demo tasks. */
#ifndef BASIC_SCHED_H
#define BASIC_SCHED_H

#include <stdint.h>

#define MAX_TASKS 8

typedef struct {
    uint32_t esp;
    int state;                  /* 0 = free, 1 = ready, 2 = sleeping */
    void (*entry)(void);
    char name[12];
    int sx, sw;                 /* status-bar cells owned by the task (sx < 0: none) */
    uint32_t wake;              /* tick at which a sleeping task becomes ready */
} task_t;

extern task_t tasks[MAX_TASKS];
extern volatile int cur;        /* index of the running task (0 = shell) */

/* set up task 0 (the shell) and its stack canary; call once from kmain */
void sched_init(void);

/* called from irq0_stub with the interrupted task's esp; returns next task's esp */
uint32_t timer_handler(uint32_t esp);

int task_spawn(void (*fn)(void), const char *name);

/* terminate the calling task (used by the exit syscall) and switch to the
 * next ready one; returns the next task's esp, or 0 if none is ready. */
uint32_t task_exit_now(void);

/* block the calling task for at least `ms` milliseconds; other tasks keep running */
void task_sleep(uint32_t ms);

/* demo worker: per-task counter in the status bar (spawned by the shell) */
void worker(void);

/* clock: HH:MM:SS at the right end of the status bar (spawned by kmain) */
void clock_task(void);

#endif
