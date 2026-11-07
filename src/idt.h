/* idt.h - interrupt descriptor table, PIC/PIT, timer tick, exception reporting. */
#ifndef BASIC_IDT_H
#define BASIC_IDT_H

#include <stdint.h>

#define TIMER_HZ 100

extern volatile uint32_t ticks;         /* incremented by the timer interrupt */

void idt_init(void);
void pic_init(void);
void pit_init(void);

/* interrupt flag helpers: save flags + cli, restore flags */
uint32_t irq_save(void);
void irq_restore(uint32_t f);

#endif
