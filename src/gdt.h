/* gdt.h - Global Descriptor Table (flat 4 GB; ring-3 code/data present, unused for now). */
#ifndef BASIC_GDT_H
#define BASIC_GDT_H

#include <stdint.h>

struct dt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));

void gdt_init(void);

#endif
