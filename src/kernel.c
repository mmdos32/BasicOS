/* kernel.c - BasicOS v0.12
 * Entry point only: kmain and init.
 * The modular layout is now complete (part 3): keyboard in src/keyboard.c,
 * the scheduler in src/sched.c, the in-memory filesystem in src/fs.c and
 * the shell in src/shell.c. The middle layer lives in src/gdt.c, src/idt.c,
 * src/paging.c, src/heap.c, src/rtc.c and src/panic.c (part 2); VGA/serial
 * output and the small libc live in src/vga.c, src/serial.c and src/klib.c
 * (part 1). The version lives in src/version.h.
 */
#include <stdint.h>

#include "gdt.h"
#include "heap.h"
#include "idt.h"
#include "io.h"
#include "keyboard.h"
#include "paging.h"
#include "sched.h"
#include "serial.h"
#include "shell.h"
#include "tss.h"
#include "version.h"
#include "vga.h"

/* ---------------------------------------------------------------- boot info */
struct mb_info { uint32_t flags, mem_lower, mem_upper; };
uint32_t ram_mb = 0;

/* ---------------------------------------------------------------- entry */
void kmain(uint32_t magic, struct mb_info *mbi) {
    if (magic == 0x2BADB002 && (mbi->flags & 1)) ram_mb = mbi->mem_upper / 1024 + 1;

    serial_init();
    gdt_init();
    tss_init();
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

    sched_init();
    task_spawn(clock_task, "clock");

    cpu_sti();
    shell();
}
