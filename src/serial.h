/* serial.h - COM1 log: everything on screen is mirrored to the serial port. */
#ifndef BASIC_SERIAL_H
#define BASIC_SERIAL_H

void serial_init(void);
void serial_mirror(char c);

#endif
