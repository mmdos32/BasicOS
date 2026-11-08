/* shell.h - the interactive shell (commands, history, line editing). */
#ifndef BASIC_SHELL_H
#define BASIC_SHELL_H

#include <stdint.h>

/* RAM size in MB, filled in by kmain from the multiboot info (0 if unknown). */
extern uint32_t ram_mb;

void shell(void);

#endif
