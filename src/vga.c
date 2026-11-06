/* vga.c - VGA text output (row 0 is the status bar). */
#include "vga.h"
#include "io.h"
#include "klib.h"
#include "serial.h"

#define VGA ((volatile uint16_t *)0xB8000)

static int cx = 0, cy = 1;              /* row 0 is the status bar */
uint8_t color = 0x0F;
uint8_t text_color = 0x0F;      /* set by the "color" command */

void vga_at(int x, int y, char c, uint8_t col) {
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

static int wrapped = 0;     /* the cursor auto-wrapped after filling the last column */

void putc(char c) {
    serial_mirror(c);
    if (c == '\n') {
        if (wrapped) wrapped = 0;       /* already at the start of a fresh line: no blank line */
        else { cx = 0; cy++; }
    } else if (c == '\b') {
        wrapped = 0;
        if (cx > 0) { cx--; vga_at(cx, cy, ' ', color); }
        else if (cy > 1) { cy--; cx = COLS - 1; vga_at(cx, cy, ' ', color); }   /* back over a wrap */
    } else if (c == '\t') {
        wrapped = 0;
        cx = (cx + 4) & ~3;
    } else {
        wrapped = 0;
        vga_at(cx, cy, c, color); cx++;
    }
    if (cx >= COLS) { cx = 0; cy++; if (c != '\n' && c != '\b') wrapped = 1; }
    if (cy >= ROWS) scroll();
    cursor_update();
}

void puts(const char *s) { while (*s) putc(*s++); }

void put_uint(uint32_t n) { char b[12]; utoa(n, b); puts(b); }

void put_hex(uint32_t n) {
    puts("0x");
    for (int i = 28; i >= 0; i -= 4) putc("0123456789ABCDEF"[(n >> i) & 0xF]);
}

void put_2(uint32_t n) { if (n < 10) putc('0'); put_uint(n); }

void clear_screen(void) {
    for (int y = 1; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) vga_at(x, y, ' ', color);
    cx = 0; cy = 1; wrapped = 0;
    cursor_update();
}
