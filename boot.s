/* boot.s - multiboot header, entry point, interrupt stubs (i386, AT&T syntax) */
.set MB_MAGIC,    0x1BADB002
.set MB_FLAGS,    0x00000003          /* align modules + memory info */
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .text
.global _start
_start:
    cli
    mov $stack_top, %esp
    call kmain
1:  hlt
    jmp 1b

/* IRQ0 (PIT timer): save state, let the scheduler pick the next stack, restore */
.global irq0_stub
irq0_stub:
    pusha
    push %esp
    call timer_handler      /* returns the new task's esp in eax */
    add $4, %esp
    mov %eax, %esp
    popa
    iret

/* IRQ1 (PS/2 keyboard) */
.global irq1_stub
irq1_stub:
    pusha
    call keyboard_handler
    popa
    iret

/* Any CPU exception: print message and halt */
.global exc_stub
exc_stub:
    cli
    call exception_handler
1:  hlt
    jmp 1b
