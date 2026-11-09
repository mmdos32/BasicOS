/* gdt.c - Global Descriptor Table setup. */
#include "gdt.h"

struct gdt_entry { uint16_t limit_lo, base_lo; uint8_t base_mid, access, gran, base_hi; } __attribute__((packed));

static struct gdt_entry gdt[5];
static struct dt_ptr gdtr;

static void gdt_set(int i, uint8_t access, uint8_t gran) {
    gdt[i].limit_lo = 0xFFFF; gdt[i].base_lo = 0; gdt[i].base_mid = 0;
    gdt[i].access = access; gdt[i].gran = gran; gdt[i].base_hi = 0;
}

void gdt_init(void) {
    gdt_set(1, 0x9A, 0xCF);             /* code, ring 0, flat 4GB */
    gdt_set(2, 0x92, 0xCF);             /* data, ring 0, flat 4GB */
    gdt_set(3, 0xFA, 0xCF);             /* code, ring 3, flat 4GB (unused for now) */
    gdt_set(4, 0xF2, 0xCF);             /* data, ring 3, flat 4GB (unused for now) */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint32_t)&gdt;
    __asm__ volatile("lgdt (%0)" :: "r"(&gdtr));
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%fs\n mov %%ax, %%gs\n mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n" ::: "ax");
}
