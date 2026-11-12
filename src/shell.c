/* shell.c - the interactive shell: commands, history, line editing, reboot. */
#include <stdint.h>
#include <stddef.h>

#include "fs.h"
#include "gdt.h"
#include "heap.h"
#include "idt.h"
#include "io.h"
#include "keyboard.h"
#include "klib.h"
#include "rtc.h"
#include "sched.h"
#include "shell.h"
#include "syscall.h"
#include "version.h"
#include "vga.h"

/* ---------------------------------------------------------------- reboot */
static void reboot(void) {
    cpu_cli();
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) ;   /* wait for the 8042, but not forever */
    outb(0x64, 0xFE);                                       /* pulse the CPU reset line */
    for (volatile int i = 0; i < 1000000; i++) ;
    struct dt_ptr empty = { 0, 0 };                         /* fallback: fault with no IDT -> triple fault */
    __asm__ volatile("lidt (%0)\n ud2" :: "r"(&empty));
    for (;;) cpu_halt();
}

/* ---------------------------------------------------------------- line editing */
#define HIST_N 8
static char hist[HIST_N][80];
static int hist_count = 0;

static void hist_add(const char *line) {
    if (!line[0]) return;
    if (hist_count > 0 && !strcmp(hist[(hist_count - 1) % HIST_N], line)) return;
    char *dst = hist[hist_count % HIST_N];
    int i = 0;
    while (line[i] && i < 79) { dst[i] = line[i]; i++; }
    dst[i] = 0;
    hist_count++;
}

static void readline(char *buf, int max) {
    int len = 0;
    int pos = hist_count;                               /* history cursor */
    for (;;) {
        uint8_t c = getchar();
        if (c == '\n') { putc('\n'); buf[len] = 0; return; }
        if (c == 3) {                          /* ctrl-c: drop the line, fresh prompt */
            while (len > 0) { len--; putc('\b'); }
            puts("^C\n"); buf[0] = 0; return;
        }
        if (c == 12) {                         /* ctrl-l: repaint the screen */
            clear_screen();
            color = 0x0A; puts("basicos> "); color = text_color;
            for (int i = 0; i < len; i++) putc(buf[i]);
            continue;
        }
        if (c == KEY_UP || c == KEY_DOWN) {
            if (c == KEY_UP && pos > 0 && pos > hist_count - HIST_N) pos--;
            else if (c == KEY_DOWN && pos < hist_count) pos++;
            else continue;
            while (len > 0) { len--; putc('\b'); }      /* erase current input */
            if (pos < hist_count) {
                const char *h = hist[pos % HIST_N];
                while (h[len] && len < max - 1) { buf[len] = h[len]; putc(h[len]); len++; }
            }
            continue;
        }
        if (c == '\b') { if (len > 0) { len--; putc('\b'); } }
        else if (c >= 32 && c < 127 && len < max - 1) { buf[len++] = c; putc(c); }
    }
}

static void cmd_help(void) {
    puts("General (up/down arrows: history):\n"
         "  help, ver, clear, uptime, date, mem, history, reboot, halt\n"
         "  caps lock toggles case, ctrl-c cancels the line, ctrl-l clears the screen\n"
         "  echo <text>         print text\n"
         "  color <1-15>        set text color\n"
         "  sleep <seconds>     pause the shell (other tasks keep running)\n"
         "Tasks:\n"
         "  spawn, ps, kill <id>\n"
         "Files (kept in RAM):\n"
         "  ls, cat <f>, rm <f>\n"
         "  write <f> <text>    create or overwrite\n"
         "  append <f> <text>   add to the end of a file\n"
         "  cp <src> <dst>, mv <old> <new>\n"
         "Debug:\n"
         "  crash <null|code|div|ud>   trigger a CPU exception on purpose\n"
         "  syscall           test the syscall gate from ring 3\n");
}

/* splits "first rest" in place; returns the trimmed rest */
static char *split2(char *arg) {
    char *rest = arg;
    while (*rest && *rest != ' ') rest++;
    if (*rest) { *rest++ = 0; while (*rest == ' ') rest++; }
    return rest;
}

void shell(void) {
    char line[80];
    puts("Type 'help' for a list of commands.\n");
    for (;;) {
        color = 0x0A; puts("basicos> "); color = text_color;
        readline(line, sizeof(line));
        char *start = trim(line);
        if (start[0] == 0) continue;
        hist_add(start);

        char *cmd = start, *arg = start;
        while (*arg && *arg != ' ') arg++;
        if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; }

        if (!strcmp(cmd, "help")) cmd_help();
        else if (!strcmp(cmd, "ver")) puts(OS_NAME " v" OS_VERSION "\n");
        else if (!strcmp(cmd, "clear")) clear_screen();
        else if (!strcmp(cmd, "echo")) { puts(arg); putc('\n'); }
        else if (!strcmp(cmd, "uptime")) { put_uint(ticks / TIMER_HZ); puts(" s\n"); }
        else if (!strcmp(cmd, "mem")) {
            uint32_t used, freeb;
            heap_stats(&used, &freeb);
            if (ram_mb) { puts("RAM: "); put_uint(ram_mb); puts(" MB\n"); }
            puts("heap used: "); put_uint(used); puts(" bytes, free: ");
            put_uint(freeb); puts(" bytes\n");
            puts("paging: on (first 16 MB mapped, kernel code read-only)\n");
        }
        else if (!strcmp(cmd, "date")) {
            rtc_t t; rtc_read(&t);
            put_uint(t.year); putc('-'); put_2(t.mon); putc('-'); put_2(t.day); putc(' ');
            put_2(t.hour); putc(':'); put_2(t.min); putc(':'); put_2(t.sec);
            puts("  (RTC, often UTC)\n");
        }
        else if (!strcmp(cmd, "color")) {
            int c = atoi(arg);
            if (c < 1 || c > 15) puts("usage: color <1-15>\n");
            else { text_color = (uint8_t)c; color = text_color; puts("ok\n"); }
        }
        else if (!strcmp(cmd, "spawn")) {
            int id = task_spawn(worker, "counter");
            if (id < 0) puts("no free task slots\n");
            else { puts("started task "); put_uint(id); putc('\n'); }
        }
        else if (!strcmp(cmd, "ps")) {
            for (int i = 0; i < MAX_TASKS; i++) {
                if (!tasks[i].state) continue;
                put_uint(i); puts("  "); puts(tasks[i].name);
                for (size_t k = strlen(tasks[i].name); k < 10; k++) putc(' ');
                puts(i == cur ? "running" : tasks[i].state == 2 ? "sleeping" : "ready");
                putc('\n');
            }
        }
        else if (!strcmp(cmd, "kill")) {
            int id = atoi(arg);
            if (id <= 0 || id >= MAX_TASKS || !tasks[id].state) puts("no such task\n");
            else {
                tasks[id].state = 0;
                if (tasks[id].sx >= 0)
                    for (int k = 0; k < tasks[id].sw; k++) vga_at(tasks[id].sx + k, 0, ' ', 0x1F);
                puts("killed\n");
            }
        }
        else if (!strcmp(cmd, "ls")) {
            int count = 0;
            for (int i = 0; i < MAX_FILES; i++) {
                if (!files[i].used) continue;
                puts(files[i].name);
                for (size_t k = strlen(files[i].name); k < NAME_LEN + 1; k++) putc(' ');
                put_uint(files[i].size); puts(" bytes\n");
                count++;
            }
            if (!count) puts("(no files)\n");
        }
        else if (!strcmp(cmd, "write")) {
            char *text = split2(arg);
            if (*arg == 0) puts("usage: write <file> <text>\n"); else fs_err(fs_write(arg, text));
        }
        else if (!strcmp(cmd, "append")) {
            char *text = split2(arg);
            if (*arg == 0) puts("usage: append <file> <text>\n"); else fs_err(fs_append(arg, text));
        }
        else if (!strcmp(cmd, "cp")) {
            char *dst = split2(arg);
            if (*arg == 0 || *dst == 0) puts("usage: cp <src> <dst>\n"); else fs_err(fs_copy(arg, dst));
        }
        else if (!strcmp(cmd, "mv")) {
            char *dst = split2(arg);
            if (*arg == 0 || *dst == 0) puts("usage: mv <old> <new>\n"); else fs_err(fs_rename(arg, dst));
        }
        else if (!strcmp(cmd, "cat")) {
            file_t *f = fs_find(arg);
            if (!f) puts("no such file\n");
            else { puts(f->data); putc('\n'); }
        }
        else if (!strcmp(cmd, "rm")) {
            if (fs_remove(arg) < 0) puts("no such file\n"); else puts("removed\n");
        }
        else if (!strcmp(cmd, "history")) {
            int start = hist_count > HIST_N ? hist_count - HIST_N : 0;
            for (int i = start; i < hist_count; i++) {
                put_uint(i + 1); puts("  "); puts(hist[i % HIST_N]); putc('\n');
            }
        }
        else if (!strcmp(cmd, "sleep")) {
            int sec = atoi(arg);
            if (sec < 1 || sec > 3600) puts("usage: sleep <1-3600>\n");
            else task_sleep((uint32_t)sec * 1000);
        }
        else if (!strcmp(cmd, "crash")) {
            if (!strcmp(arg, "null")) { volatile uint32_t *p = (volatile uint32_t *)0; *p = 1; }
            else if (!strcmp(arg, "code")) { volatile uint8_t *p = (volatile uint8_t *)(uint32_t)timer_handler; *p = 0; }
            else if (!strcmp(arg, "div")) { volatile int z = 0; volatile int r = 1 / z; (void)r; }
            else if (!strcmp(arg, "ud")) __asm__ volatile("ud2");
            else puts("usage: crash <null|code|div|ud>\n");
        }
        else if (!strcmp(cmd, "syscall")) {
            int id = task_spawn(ring3_test, "ring3test");
            if (id < 0) puts("no free task slots\n");
            else { puts("started task "); put_uint(id); putc('\n'); }
        }
        else if (!strcmp(cmd, "reboot")) reboot();
        else if (!strcmp(cmd, "halt")) {
            puts("System halted.\n");
            cpu_cli();
            for (;;) cpu_halt();
        }
        else { puts("unknown command: "); puts(cmd); putc('\n'); }
    }
}
