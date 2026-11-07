/* kernel.c - BasicOS v0.8
 * Keyboard (history, caps lock, ctrl keys), preemptive multitasking with
 * sleep, in-memory filesystem, shell.
 * The middle layer now lives in its own modules (modular layout, part 2):
 * GDT/IDT/PIC/PIT in src/gdt.c and src/idt.c, paging in src/paging.c, heap
 * in src/heap.c, RTC in src/rtc.c, panic in src/panic.c.
 * VGA/serial output and the small libc live in src/vga.c, src/serial.c and
 * src/klib.c (modular layout, part 1).
 */
#include <stdint.h>
#include <stddef.h>

#include "io.h"
#include "klib.h"
#include "vga.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "heap.h"
#include "rtc.h"
#include "panic.h"

#define OS_NAME    "BasicOS"
#define OS_VERSION "0.8"

/* ---------------------------------------------------------------- RAM filesystem */
#define MAX_FILES 16
#define NAME_LEN  16

typedef struct {
    char name[NAME_LEN];
    char *data;
    uint32_t size;
    int used;
} file_t;

static file_t files[MAX_FILES];

static file_t *fs_find(const char *name) {
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].used && !strcmp(files[i].name, name)) return &files[i];
    return NULL;
}

/* returns 0 on success, -1 bad name, -2 out of file slots, -3 out of memory */
static int fs_write(const char *name, const char *text) {
    size_t nl = strlen(name);
    if (nl == 0 || nl >= NAME_LEN) return -1;

    uint32_t len = strlen(text);
    char *buf = kmalloc(len + 1);
    if (!buf) return -3;
    memcpy(buf, text, len + 1);

    file_t *f = fs_find(name);
    if (f) {
        kfree(f->data);
    } else {
        for (int i = 0; i < MAX_FILES; i++) if (!files[i].used) { f = &files[i]; break; }
        if (!f) { kfree(buf); return -2; }
        memcpy(f->name, name, nl + 1);
        f->used = 1;
    }
    f->data = buf;
    f->size = len;
    return 0;
}

static int fs_remove(const char *name) {
    file_t *f = fs_find(name);
    if (!f) return -1;
    kfree(f->data);
    f->used = 0;
    return 0;
}

static int fs_append(const char *name, const char *text) {
    file_t *f = fs_find(name);
    if (!f) return fs_write(name, text);
    uint32_t add = strlen(text);
    char *buf = kmalloc(f->size + add + 1);
    if (!buf) return -3;
    memcpy(buf, f->data, f->size);
    memcpy(buf + f->size, text, add + 1);
    kfree(f->data);
    f->data = buf;
    f->size += add;
    return 0;
}

static int fs_copy(const char *src, const char *dst) {
    file_t *f = fs_find(src);
    if (!f) return -4;
    return fs_write(dst, f->data);
}

static int fs_rename(const char *from, const char *to) {
    file_t *f = fs_find(from);
    if (!f) return -4;
    size_t n = strlen(to);
    if (n == 0 || n >= NAME_LEN) return -1;
    file_t *g = fs_find(to);
    if (g && g != f) return -5;
    memcpy(f->name, to, n + 1);
    return 0;
}

static void fs_err(int r) {
    switch (r) {
    case -1: puts("bad file name (max 15 chars)\n"); break;
    case -2: puts("too many files\n"); break;
    case -3: puts("out of memory\n"); break;
    case -4: puts("no such file\n"); break;
    case -5: puts("target already exists\n"); break;
    default: puts("ok\n");
    }
}

/* ---------------------------------------------------------------- multitasking */
#define MAX_TASKS 8
#define STACK_SZ  4096

typedef struct {
    uint32_t esp;
    int state;                  /* 0 = free, 1 = ready, 2 = sleeping */
    void (*entry)(void);
    char name[12];
    int sx, sw;                 /* status-bar cells owned by the task (sx < 0: none) */
    uint32_t wake;              /* tick at which a sleeping task becomes ready */
} task_t;

#define STACK_CANARY 0xDEADC0DEu
extern char stack_bottom[];                     /* the boot stack used by the shell (boot.s) */

static task_t tasks[MAX_TASKS];
static uint8_t stacks[MAX_TASKS][STACK_SZ] __attribute__((aligned(16)));
static volatile int cur = 0;

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

static void task_main(void) {
    tasks[cur].entry();
    task_exit();
}

static void set_name(char *dst, const char *src, int max) {
    int j = 0;
    while (src[j] && j < max - 1) { dst[j] = src[j]; j++; }
    dst[j] = 0;
}

static int task_spawn(void (*fn)(void), const char *name) {
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
static void task_sleep(uint32_t ms) {
    int me = cur;
    uint32_t t = ms * TIMER_HZ / 1000;
    if (t == 0) t = 1;
    tasks[me].wake = ticks + t;
    tasks[me].state = 2;
    while (tasks[me].state == 2) cpu_halt();
}

/* demo worker: shows a per-task counter in the status bar (row 0) */
static void worker(void) {
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
static void clock_task(void) {
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

/* ---------------------------------------------------------------- keyboard */
static const char km[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0,'*', 0,' '
};
static const char kms[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0,'*', 0,' '
};

#define KBUF 128
#define KEY_UP   0x80
#define KEY_DOWN 0x81
static volatile uint8_t kbuf[KBUF];
static volatile int kb_head = 0, kb_tail = 0;
static int lshift = 0, rshift = 0, lctrl = 0, rctrl = 0, ext = 0;
static int caps = 0;

static void kb_push(uint8_t c) {
    int next = (kb_head + 1) % KBUF;
    if (next != kb_tail) { kbuf[kb_head] = c; kb_head = next; }
}

/* caps lock LED: 0xED then a byte (bit 2 = caps). Bounded waits only, so a
 * stuck 8042 can never hang the keyboard interrupt handler; the extra ack
 * the keyboard sends back matches no key and is harmless. */
static void kbd_set_leds(void) {
    for (int i = 0; i < 10000 && (inb(0x64) & 2); i++) ;
    outb(0x60, 0xED); io_wait();
    for (int i = 0; i < 10000 && !(inb(0x64) & 1); i++) ;
    if (inb(0x60) != 0xFA) return;                    /* no ack: leave the LEDs alone */
    for (int i = 0; i < 10000 && (inb(0x64) & 2); i++) ;
    outb(0x60, caps ? 0x04 : 0x00);
}

void keyboard_handler(void) {
    uint8_t sc = inb(0x60);
    if (sc == 0xE0) { ext = 1; }                        /* extended-key prefix */
    else if (ext) {
        ext = 0;
        if (sc == 0x48) kb_push(KEY_UP);
        else if (sc == 0x50) kb_push(KEY_DOWN);
        else if (sc == 0x1D) rctrl = 1;
        else if (sc == 0x9D) rctrl = 0;
    }
    else if (sc == 0x2A) lshift = 1;
    else if (sc == 0xAA) lshift = 0;
    else if (sc == 0x36) rshift = 1;
    else if (sc == 0xB6) rshift = 0;
    else if (sc == 0x1D) lctrl = 1;
    else if (sc == 0x9D) lctrl = 0;
    else if (sc == 0x3A) { caps = !caps; kbd_set_leds(); }   /* caps lock toggles on press */
    else if (sc < 58) {
        char c = (lshift || rshift) ? kms[sc] : km[sc];
        if (lctrl || rctrl) {
            /* ctrl + letter -> control code (ctrl-c = 3, ctrl-l = 12, ...);
             * ctrl + anything else is ignored */
            if (c >= 'a' && c <= 'z') kb_push((uint8_t)(c - 'a' + 1));
            else if (c >= 'A' && c <= 'Z') kb_push((uint8_t)(c - 'A' + 1));
        } else {
            if (caps) {
                if (c >= 'a' && c <= 'z') c -= 32;      /* caps makes letters upper */
                else if (c >= 'A' && c <= 'Z') c += 32;  /* shift + caps cancels out */
            }
            if (c) kb_push((uint8_t)c);
        }
    }
    outb(0x20, 0x20);           /* EOI */
}

/* a byte left in the 8042 output buffer (e.g. by the bootloader) blocks further
 * keyboard interrupts until it is read, so empty it before enabling IRQ1 */
static void kbd_init(void) {
    for (int i = 0; i < 64 && (inb(0x64) & 1); i++) { inb(0x60); io_wait(); }
}

static uint8_t getchar(void) {
    while (kb_head == kb_tail) cpu_halt();
    uint8_t c = kbuf[kb_tail];
    kb_tail = (kb_tail + 1) % KBUF;
    return c;
}

/* ---------------------------------------------------------------- boot info */
struct mb_info { uint32_t flags, mem_lower, mem_upper; };
static uint32_t ram_mb = 0;

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

/* ---------------------------------------------------------------- shell */
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
         "  crash <null|code|div|ud>   trigger a CPU exception on purpose\n");
}

/* splits "first rest" in place; returns the trimmed rest */
static char *split2(char *arg) {
    char *rest = arg;
    while (*rest && *rest != ' ') rest++;
    if (*rest) { *rest++ = 0; while (*rest == ' ') rest++; }
    return rest;
}

static void shell(void) {
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
        else if (!strcmp(cmd, "reboot")) reboot();
        else if (!strcmp(cmd, "halt")) {
            puts("System halted.\n");
            cpu_cli();
            for (;;) cpu_halt();
        }
        else { puts("unknown command: "); puts(cmd); putc('\n'); }
    }
}

/* ---------------------------------------------------------------- entry */
void kmain(uint32_t magic, struct mb_info *mbi) {
    if (magic == 0x2BADB002 && (mbi->flags & 1)) ram_mb = mbi->mem_upper / 1024 + 1;

    serial_init();
    gdt_init();
    idt_init();
    pic_init();
    pit_init();
    kbd_init();
    heap_init();
    paging_init();

    for (int i = 0; i < COLS; i++) vga_at(i, 0, ' ', 0x1F);
    const char *title = " " OS_NAME " ";
    for (int i = 0; title[i]; i++) vga_at(i, 0, title[i], 0x1F);
    clear_screen();

    puts(OS_NAME " v" OS_VERSION " booted.\n\n");

    *(uint32_t *)stack_bottom = STACK_CANARY;
    tasks[0].state = 1;                 /* the shell is task 0 */
    set_name(tasks[0].name, "shell", sizeof(tasks[0].name));
    tasks[0].sx = -1;
    task_spawn(clock_task, "clock");

    cpu_sti();
    shell();
}
