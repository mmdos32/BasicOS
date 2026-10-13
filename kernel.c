/* kernel.c - BasicOS v0.4
 * VGA text + serial log, GDT/IDT, PIC/PIT, paging (NULL guard, read-only code),
 * keyboard (with history), preemptive multitasking with sleep, heap allocator,
 * in-memory filesystem, RTC clock, exception diagnostics, shell.
 */
#include <stdint.h>
#include <stddef.h>

#define OS_NAME    "BasicOS"
#define OS_VERSION "0.4"

/* ---------------------------------------------------------------- port I/O */
static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* ---------------------------------------------------------------- libc bits */
void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dp = d; const uint8_t *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memset(void *d, int v, size_t n) {
    uint8_t *dp = d;
    while (n--) *dp++ = (uint8_t)v;
    return d;
}

static size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

static int atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n;
}

static void utoa(uint32_t n, char *buf) {
    char tmp[12]; int i = 0, j = 0;
    if (n == 0) tmp[i++] = '0';
    while (n) { tmp[i++] = '0' + n % 10; n /= 10; }
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
}

/* ---------------------------------------------------------------- VGA text */
#define VGA ((volatile uint16_t *)0xB8000)
#define COLS 80
#define ROWS 25

static int cx = 0, cy = 1;              /* row 0 is the status bar */
static uint8_t color = 0x0F;
static uint8_t text_color = 0x0F;      /* set by the "color" command */

static void vga_at(int x, int y, char c, uint8_t col) {
    VGA[y * COLS + x] = ((uint16_t)col << 8) | (uint8_t)c;
}

static void cursor_update(void) {
    uint16_t pos = cy * COLS + cx;
    outb(0x3D4, 14); outb(0x3D5, pos >> 8);
    outb(0x3D4, 15); outb(0x3D5, pos & 0xFF);
}

static void scroll(void) {
    for (int i = COLS; i < COLS * (ROWS - 1); i++) VGA[i] = VGA[i + COLS];
    for (int i = 0; i < COLS; i++) vga_at(i, ROWS - 1, ' ', color);
    cy = ROWS - 1;
}

/* ---------------------------------------------------------------- serial (COM1) log
 * Everything printed on screen is mirrored to COM1 (QEMU: -serial stdio).
 * The port is probed with a loopback test, so machines without one stay fast.
 */
static int serial_ok = 0;

static void serial_init(void) {
    outb(0x3F9, 0x00);                  /* no interrupts */
    outb(0x3FB, 0x80);                  /* DLAB on */
    outb(0x3F8, 0x03); outb(0x3F9, 0x00);   /* 38400 baud */
    outb(0x3FB, 0x03);                  /* 8N1 */
    outb(0x3FA, 0xC7);                  /* FIFO */
    outb(0x3FC, 0x1E);                  /* loopback mode */
    outb(0x3F8, 0xAE);
    if (inb(0x3F8) != 0xAE) return;     /* no UART here */
    outb(0x3FC, 0x0F);                  /* normal operation */
    serial_ok = 1;
}

static void serial_put(char c) {
    for (int i = 0; i < 20000 && !(inb(0x3FD) & 0x20); i++) ;
    outb(0x3F8, (uint8_t)c);
}

static void serial_mirror(char c) {
    if (!serial_ok) return;
    if (c == '\n') { serial_put('\r'); serial_put('\n'); }
    else if (c == '\b') { serial_put('\b'); serial_put(' '); serial_put('\b'); }
    else if (c == '\t') serial_put(' ');
    else serial_put(c);
}

static void putc(char c) {
    serial_mirror(c);
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\b') { if (cx > 0) { cx--; vga_at(cx, cy, ' ', color); } }
    else if (c == '\t') { cx = (cx + 4) & ~3; }
    else { vga_at(cx, cy, c, color); cx++; }
    if (cx >= COLS) { cx = 0; cy++; }
    if (cy >= ROWS) scroll();
    cursor_update();
}

static void puts(const char *s) { while (*s) putc(*s++); }

static void put_uint(uint32_t n) { char b[12]; utoa(n, b); puts(b); }

static void put_hex(uint32_t n) {
    puts("0x");
    for (int i = 28; i >= 0; i -= 4) putc("0123456789ABCDEF"[(n >> i) & 0xF]);
}

static void put_2(uint32_t n) { if (n < 10) putc('0'); put_uint(n); }

static void clear_screen(void) {
    for (int y = 1; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) vga_at(x, y, ' ', color);
    cx = 0; cy = 1;
    cursor_update();
}

/* ---------------------------------------------------------------- GDT */
struct gdt_entry { uint16_t limit_lo, base_lo; uint8_t base_mid, access, gran, base_hi; } __attribute__((packed));
struct dt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));

static struct gdt_entry gdt[3];
static struct dt_ptr gdtr;

static void gdt_set(int i, uint8_t access, uint8_t gran) {
    gdt[i].limit_lo = 0xFFFF; gdt[i].base_lo = 0; gdt[i].base_mid = 0;
    gdt[i].access = access; gdt[i].gran = gran; gdt[i].base_hi = 0;
}

static void gdt_init(void) {
    gdt_set(1, 0x9A, 0xCF);             /* code, ring 0, flat 4GB */
    gdt_set(2, 0x92, 0xCF);             /* data, ring 0, flat 4GB */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint32_t)&gdt;
    __asm__ volatile("lgdt (%0)" :: "r"(&gdtr));
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%fs\n mov %%ax, %%gs\n mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n" ::: "ax");
}

/* ---------------------------------------------------------------- IDT / PIC / PIT */
struct idt_entry { uint16_t off_lo, sel; uint8_t zero, flags; uint16_t off_hi; } __attribute__((packed));
static struct idt_entry idt[256];
static struct dt_ptr idtr;

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void (*exc_table[32])(void);

static void idt_set(int i, void (*h)(void)) {
    uint32_t a = (uint32_t)h;
    idt[i].off_lo = a & 0xFFFF; idt[i].sel = 0x08; idt[i].zero = 0;
    idt[i].flags = 0x8E; idt[i].off_hi = a >> 16;
}

static void idt_init(void) {
    for (int i = 0; i < 32; i++) idt_set(i, exc_table[i]);
    idt_set(32, irq0_stub);
    idt_set(33, irq1_stub);
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)&idt;
    __asm__ volatile("lidt (%0)" :: "r"(&idtr));
}

static void pic_init(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFC);                    /* unmask IRQ0 (timer) + IRQ1 (keyboard) */
    outb(0xA1, 0xFF);
}

#define TIMER_HZ 100
static volatile uint32_t ticks = 0;

static void pit_init(void) {
    uint16_t div = 1193182 / TIMER_HZ;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, div >> 8);
}

/* ---------------------------------------------------------------- exceptions */
struct exc_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;    /* pusha */
    uint32_t vector, err;
    uint32_t eip, cs, eflags;                           /* pushed by the CPU */
};

static const char *exc_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range exceeded",
    "Invalid opcode", "Device not available", "Double fault", "Coprocessor overrun",
    "Invalid TSS", "Segment not present", "Stack fault", "General protection fault",
    "Page fault", "Reserved", "x87 FPU error", "Alignment check", "Machine check",
    "SIMD FP error"
};

void exception_handler(struct exc_frame *f) {
    color = 0x4F;
    puts("\n*** CPU EXCEPTION: ");
    puts(f->vector < 32 && exc_names[f->vector] ? exc_names[f->vector] : "Reserved");
    puts(" ***\n");
    puts("vector "); put_uint(f->vector);
    puts("  error "); put_hex(f->err);
    if (f->vector == 14) {
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        puts("\nfaulting address "); put_hex(cr2);
        puts((f->err & 1) ? " (protection violation, " : " (page not present, ");
        puts((f->err & 2) ? "write)" : "read)");
    }
    puts("\nEIP "); put_hex(f->eip);
    puts("  CS "); put_hex(f->cs);
    puts("  EFLAGS "); put_hex(f->eflags);
    puts("\nEAX "); put_hex(f->eax);
    puts("  EBX "); put_hex(f->ebx);
    puts("\nECX "); put_hex(f->ecx);
    puts("  EDX "); put_hex(f->edx);
    puts("\nSystem halted.\n");
}

/* ---------------------------------------------------------------- interrupt flag helpers */
static uint32_t irq_save(void) {
    uint32_t f;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}

static void irq_restore(uint32_t f) {
    __asm__ volatile("push %0; popf" :: "r"(f) : "memory", "cc");
}

/* ---------------------------------------------------------------- RTC clock (CMOS) */
typedef struct { uint8_t sec, min, hour, day, mon; uint16_t year; } rtc_t;

static uint8_t cmos(uint8_t reg) { outb(0x70, reg); return inb(0x71); }

#define BCD(x) ((uint8_t)(((x) & 0x0F) + ((x) >> 4) * 10))

static void rtc_read(rtc_t *t) {
    /* the index/data ports are shared, so no task switch may happen in between */
    uint32_t fl = irq_save();
    for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++) ;
    uint8_t s = cmos(0x00), m = cmos(0x02), h = cmos(0x04);
    uint8_t d = cmos(0x07), mo = cmos(0x08), y = cmos(0x09), b = cmos(0x0B);
    irq_restore(fl);

    int pm = h & 0x80;
    h &= 0x7F;
    if (!(b & 4)) { s = BCD(s); m = BCD(m); h = BCD(h); d = BCD(d); mo = BCD(mo); y = BCD(y); }
    if (!(b & 2)) h = pm ? (h % 12) + 12 : h % 12;      /* 12-hour mode */
    t->sec = s; t->min = m; t->hour = h; t->day = d; t->mon = mo; t->year = 2000 + y;
}

/* ---------------------------------------------------------------- heap allocator
 * First-fit free list over a static arena. Only the shell (task 0) allocates,
 * so no locking is needed.
 */
#define HEAP_SIZE (256 * 1024)

typedef struct block {
    uint32_t size;
    uint32_t free;
    struct block *next;
    uint32_t pad;               /* keeps the header at 16 bytes */
} block_t;

static uint8_t heap[HEAP_SIZE] __attribute__((aligned(16)));
static block_t *heap_head;

static void heap_init(void) {
    heap_head = (block_t *)heap;
    heap_head->size = HEAP_SIZE - sizeof(block_t);
    heap_head->free = 1;
    heap_head->next = NULL;
}

static void *kmalloc(uint32_t n) {
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

static void kfree(void *p) {
    if (!p) return;
    ((block_t *)p - 1)->free = 1;
    for (block_t *c = heap_head; c; c = c->next) {      /* coalesce */
        while (c->free && c->next && c->next->free) {
            c->size += sizeof(block_t) + c->next->size;
            c->next = c->next->next;
        }
    }
}

static void heap_stats(uint32_t *used, uint32_t *freeb) {
    *used = 0; *freeb = 0;
    for (block_t *b = heap_head; b; b = b->next) {
        if (b->free) *freeb += b->size; else *used += b->size;
    }
}

/* ---------------------------------------------------------------- paging
 * Identity-maps the first 16 MB with 4 KB pages. Page 0 is left unmapped, so a
 * NULL dereference faults; kernel code and read-only data are mapped read-only
 * (CR0.WP makes that apply to the kernel itself).
 */
#define PAGE_SIZE   4096
#define MAP_TABLES  4                           /* 4 x 4 MB = 16 MB */

extern char __ro_end[];                         /* from linker.ld */

static uint32_t page_dir[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[MAP_TABLES][1024] __attribute__((aligned(4096)));

static void paging_init(void) {
    uint32_t ro_end = (uint32_t)__ro_end;
    for (int t = 0; t < MAP_TABLES; t++) {
        for (int i = 0; i < 1024; i++) {
            uint32_t addr = ((uint32_t)t * 1024 + i) * PAGE_SIZE;
            uint32_t flags = 3;                 /* present + writable */
            if (addr == 0) { page_tables[t][i] = 0; continue; }
            if (addr >= 0x100000 && addr < ro_end) flags = 1;   /* present, read-only */
            page_tables[t][i] = addr | flags;
        }
        page_dir[t] = (uint32_t)page_tables[t] | 3;
    }
    __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir));
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000u;                         /* PG + WP */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
}

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

static task_t tasks[MAX_TASKS];
static uint8_t stacks[MAX_TASKS][STACK_SZ] __attribute__((aligned(16)));
static volatile int cur = 0;

/* called from irq0_stub with the interrupted task's esp; returns next task's esp */
uint32_t timer_handler(uint32_t esp) {
    ticks++;
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
    for (;;) __asm__ volatile("hlt");
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
    while (tasks[me].state == 2) __asm__ volatile("hlt");
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
        __asm__ volatile("hlt");
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
static int shift = 0, ext = 0;

static void kb_push(uint8_t c) {
    int next = (kb_head + 1) % KBUF;
    if (next != kb_tail) { kbuf[kb_head] = c; kb_head = next; }
}

void keyboard_handler(void) {
    uint8_t sc = inb(0x60);
    if (sc == 0xE0) { ext = 1; }                        /* extended-key prefix */
    else if (ext) {
        ext = 0;
        if (sc == 0x48) kb_push(KEY_UP);
        else if (sc == 0x50) kb_push(KEY_DOWN);
    }
    else if (sc == 0x2A || sc == 0x36) shift = 1;
    else if (sc == 0xAA || sc == 0xB6) shift = 0;
    else if (sc < 58) {
        char c = shift ? kms[sc] : km[sc];
        if (c) kb_push((uint8_t)c);
    }
    outb(0x20, 0x20);           /* EOI */
}

static uint8_t getchar(void) {
    while (kb_head == kb_tail) __asm__ volatile("hlt");
    uint8_t c = kbuf[kb_tail];
    kb_tail = (kb_tail + 1) % KBUF;
    return c;
}

/* ---------------------------------------------------------------- boot info */
struct mb_info { uint32_t flags, mem_lower, mem_upper; };
static uint32_t ram_mb = 0;

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
        if (line[0] == 0) continue;
        hist_add(line);

        char *cmd = line, *arg = line;
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
        else if (!strcmp(cmd, "reboot")) {
            while (inb(0x64) & 2) ;
            outb(0x64, 0xFE);
            for (;;) __asm__ volatile("hlt");
        }
        else if (!strcmp(cmd, "halt")) {
            puts("System halted.\n");
            __asm__ volatile("cli");
            for (;;) __asm__ volatile("hlt");
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
    heap_init();
    paging_init();

    for (int i = 0; i < COLS; i++) vga_at(i, 0, ' ', 0x1F);
    const char *title = " " OS_NAME " ";
    for (int i = 0; title[i]; i++) vga_at(i, 0, title[i], 0x1F);
    clear_screen();

    puts(OS_NAME " v" OS_VERSION " booted.\n\n");

    tasks[0].state = 1;                 /* the shell is task 0 */
    set_name(tasks[0].name, "shell", sizeof(tasks[0].name));
    tasks[0].sx = -1;
    task_spawn(clock_task, "clock");

    __asm__ volatile("sti");
    shell();
}
