/* keyboard.h - PS/2 keyboard: scancode decoding, caps/shift/ctrl, input buffer. */
#ifndef BASIC_KEYBOARD_H
#define BASIC_KEYBOARD_H

#include <stdint.h>

#define KEY_UP   0x80
#define KEY_DOWN 0x81

void kbd_init(void);
uint8_t getchar(void);

/* called from the IRQ1 stub in boot.s */
void keyboard_handler(void);

#endif
