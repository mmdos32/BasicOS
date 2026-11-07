/* panic.c - fatal kernel error: print and halt. */
#include "io.h"
#include "panic.h"
#include "vga.h"

void panic(const char *msg, int id) {
    cpu_cli();
    color = 0x4F;
    puts("\n*** KERNEL PANIC: "); puts(msg);
    puts(" (task "); put_uint((uint32_t)id); puts(") ***\nSystem halted.\n");
    for (;;) cpu_halt();
}
