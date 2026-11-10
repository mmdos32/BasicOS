/* gdt.c - Global Descriptor Table setup. */
#include "gdt.h"

#include "tss.h"

struct gdt_entry { uint16_t limit_lo, base_lo; uint8_t base_mid, access, gran, base_hi; } __attribute__((packed));

static struct gdt_entry gdt[6];
static struct dt_ptr gdtr;

static void gdt_set(int i, uint8_t access, uint8_t gran) {
    gdt[i].limit_lo = 0xFFFF; gdt[i].base_lo = 0; gdt[i].base_mid = 0;
    gdt[i].access = access; gdt[i].gran = gran; gdt[i].base_hi = 0;
}

static void gdt_set_tss(int i, uint32_t base, uint16_t limit) {
    gdt[i].limit_lo = limit; gdt[i].base_lo = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].access = 0x89;           /* present, ring 0, 32-bit available TSS */
    gdt[i].gran = 0x00;             /* byte granularity, limit < 64 KiB */
    gdt[i].base_hi = (base >> 24) & 0xFF;
}

void gdt_init(void) {
    gdt_set(1, 0x9A, 0xCF);             /* code, ring 0, flat 4GB */
    gdt_set(2, 0x92, 0xCF);             /* data, ring 0, flat 4GB */
    gdt_set(3, 0xFA, 0xCF);             /* code, ring 3, flat 4GB (unused for now) */
    gdt_set(4, 0xF2, 0xCF);             /* data, ring 3, flat 4GB (unused for now) */
    gdt_set_tss(5, (uint32_t)&tss, sizeof(tss) - 1);  /* TSS (loaded by tss_init) */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint32_t)&gdt;
    __asm__ volatile("lgdt (%0)" :: "r"(&gdtr));
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%fs\n mov %%ax, %%gs\n mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n" ::: "ax");
}
