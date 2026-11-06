/* vga.h - VGA text output (row 0 is the status bar). */
#ifndef BASIC_VGA_H
#define BASIC_VGA_H

#include <stdint.h>

#define COLS 80
#define ROWS 25

extern uint8_t color;
extern uint8_t text_color;      /* set by the "color" command */

void vga_at(int x, int y, char c, uint8_t col);
void putc(char c);
void puts(const char *s);
void put_uint(uint32_t n);
void put_hex(uint32_t n);
void put_2(uint32_t n);
void clear_screen(void);

#endif
