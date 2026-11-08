/* keyboard.c - PS/2 keyboard: scancode decoding, caps/shift/ctrl, input buffer. */
#include <stdint.h>

#include "io.h"
#include "keyboard.h"

static const char km[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0,'*', 0,' '
};
static const char kms[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0,'*', 0,' '
};

#define KBUF 128
static volatile uint8_t kbuf[KBUF];
static volatile int kb_head = 0, kb_tail = 0;
static int lshift = 0, rshift = 0, lctrl = 0, rctrl = 0, ext = 0;
static int caps = 0;

static void kb_push(uint8_t c) {
    int next = (kb_head + 1) % KBUF;
    if (next != kb_tail) { kbuf[kb_head] = c; kb_head = next; }
}

/* caps lock LED: 0xED then a byte (bit 2 = caps). Bounded waits only, so a
 * stuck 8042 can never hang the keyboard interrupt handler; the extra ack
 * the keyboard sends back matches no key and is harmless. */
static void kbd_set_leds(void) {
    for (int i = 0; i < 10000 && (inb(0x64) & 2); i++) ;
    outb(0x60, 0xED); io_wait();
    for (int i = 0; i < 10000 && !(inb(0x64) & 1); i++) ;
    if (inb(0x60) != 0xFA) return;                    /* no ack: leave the LEDs alone */
    for (int i = 0; i < 10000 && (inb(0x64) & 2); i++) ;
    outb(0x60, caps ? 0x04 : 0x00);
}

void keyboard_handler(void) {
    uint8_t sc = inb(0x60);
    if (sc == 0xE0) { ext = 1; }                        /* extended-key prefix */
    else if (ext) {
        ext = 0;
        if (sc == 0x48) kb_push(KEY_UP);
        else if (sc == 0x50) kb_push(KEY_DOWN);
        else if (sc == 0x1D) rctrl = 1;
        else if (sc == 0x9D) rctrl = 0;
    }
    else if (sc == 0x2A) lshift = 1;
    else if (sc == 0xAA) lshift = 0;
    else if (sc == 0x36) rshift = 1;
    else if (sc == 0xB6) rshift = 0;
    else if (sc == 0x1D) lctrl = 1;
    else if (sc == 0x9D) lctrl = 0;
    else if (sc == 0x3A) { caps = !caps; kbd_set_leds(); }   /* caps lock toggles on press */
    else if (sc < 58) {
        char c = (lshift || rshift) ? kms[sc] : km[sc];
        if (lctrl || rctrl) {
            /* ctrl + letter -> control code (ctrl-c = 3, ctrl-l = 12, ...);
             * ctrl + anything else is ignored */
            if (c >= 'a' && c <= 'z') kb_push((uint8_t)(c - 'a' + 1));
            else if (c >= 'A' && c <= 'Z') kb_push((uint8_t)(c - 'A' + 1));
        } else {
            if (caps) {
                if (c >= 'a' && c <= 'z') c -= 32;      /* caps makes letters upper */
                else if (c >= 'A' && c <= 'Z') c += 32;  /* shift + caps cancels out */
            }
            if (c) kb_push((uint8_t)c);
        }
    }
    outb(0x20, 0x20);           /* EOI */
}

/* a byte left in the 8042 output buffer (e.g. by the bootloader) blocks further
 * keyboard interrupts until it is read, so empty it before enabling IRQ1 */
void kbd_init(void) {
    for (int i = 0; i < 64 && (inb(0x64) & 1); i++) { inb(0x60); io_wait(); }
}

uint8_t getchar(void) {
    while (kb_head == kb_tail) cpu_halt();
    uint8_t c = kbuf[kb_tail];
    kb_tail = (kb_tail + 1) % KBUF;
    return c;
}
