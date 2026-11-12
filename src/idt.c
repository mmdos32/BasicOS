/* idt.c - IDT, PIC, PIT and CPU exception reporting. */
#include <stdint.h>

#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "vga.h"

struct idt_entry { uint16_t off_lo, sel; uint8_t zero, flags; uint16_t off_hi; } __attribute__((packed));
static struct idt_entry idt[256];
static struct dt_ptr idtr;

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void syscall_stub(void);
extern void (*exc_table[32])(void);

static void idt_set_flags(int i, void (*h)(void), uint8_t flags) {
    uint32_t a = (uint32_t)h;
    idt[i].off_lo = a & 0xFFFF; idt[i].sel = 0x08; idt[i].zero = 0;
    idt[i].flags = flags; idt[i].off_hi = a >> 16;
}

static void idt_set(int i, void (*h)(void)) { idt_set_flags(i, h, 0x8E); }

void idt_init(void) {
    for (int i = 0; i < 32; i++) idt_set(i, exc_table[i]);
    idt_set(32, irq0_stub);
    idt_set(33, irq1_stub);
    idt_set_flags(0x80, syscall_stub, 0xEE);   /* int 0x80: callable from ring 3 */
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)&idt;
    __asm__ volatile("lidt (%0)" :: "r"(&idtr));
}

void pic_init(void) {
    outb(0x20, 0x11); io_wait(); outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait(); outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFC);                    /* unmask IRQ0 (timer) + IRQ1 (keyboard) */
    outb(0xA1, 0xFF);
}

volatile uint32_t ticks = 0;

void pit_init(void) {
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
uint32_t irq_save(void) {
    uint32_t f;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}

void irq_restore(uint32_t f) {
    __asm__ volatile("push %0; popf" :: "r"(f) : "memory", "cc");
}
