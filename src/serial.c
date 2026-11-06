/* serial.c - COM1 log.
 * Everything printed on screen is mirrored to COM1 (QEMU: -serial stdio).
 * The port is probed with a loopback test, so machines without one stay fast.
 */
#include "serial.h"
#include "io.h"

static int serial_ok = 0;

void serial_init(void) {
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

void serial_mirror(char c) {
    if (!serial_ok) return;
    if (c == '\n') { serial_put('\r'); serial_put('\n'); }
    else if (c == '\b') { serial_put('\b'); serial_put(' '); serial_put('\b'); }
    else if (c == '\t') serial_put(' ');
    else serial_put(c);
}
