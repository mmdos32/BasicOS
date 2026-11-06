/* io.h - port I/O and CPU control helpers (header-only). */
#ifndef BASIC_IO_H
#define BASIC_IO_H

#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* hlt/cli/sti must also be compiler barriers: loops that wait for something an
 * interrupt handler changes would otherwise be free to cache the value in a register */
static inline void cpu_halt(void) { __asm__ volatile("hlt" ::: "memory"); }
static inline void cpu_cli(void)  { __asm__ volatile("cli" ::: "memory"); }
static inline void cpu_sti(void)  { __asm__ volatile("sti" ::: "memory"); }
static inline void io_wait(void)  { outb(0x80, 0); }      /* ~1 us delay for old PIC/PIT hardware */

#endif
