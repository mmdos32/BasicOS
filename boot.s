/* boot.s - multiboot header, entry point, interrupt and exception stubs (i386, AT&T syntax) */
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
    push %ebx               /* kmain(magic, multiboot_info) */
    push %eax
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

/* CPU exceptions: build a frame (regs, vector, error code) and report it in C */
.macro EXC_NOERR num
exc\num:
    cli
    push $0
    push $\num
    jmp exc_common
.endm

.macro EXC_ERR num
exc\num:
    cli
    push $\num
    jmp exc_common
.endm

exc_common:
    pusha
    push %esp
    call exception_handler
1:  hlt
    jmp 1b

EXC_NOERR 0
EXC_NOERR 1
EXC_NOERR 2
EXC_NOERR 3
EXC_NOERR 4
EXC_NOERR 5
EXC_NOERR 6
EXC_NOERR 7
EXC_ERR 8
EXC_NOERR 9
EXC_ERR 10
EXC_ERR 11
EXC_ERR 12
EXC_ERR 13
EXC_ERR 14
EXC_NOERR 15
EXC_NOERR 16
EXC_ERR 17
EXC_NOERR 18
EXC_NOERR 19
EXC_NOERR 20
EXC_ERR 21
EXC_NOERR 22
EXC_NOERR 23
EXC_NOERR 24
EXC_NOERR 25
EXC_NOERR 26
EXC_NOERR 27
EXC_NOERR 28
EXC_ERR 29
EXC_ERR 30
EXC_NOERR 31

.section .rodata
.align 4
.global exc_table
exc_table:
    .long exc0
    .long exc1
    .long exc2
    .long exc3
    .long exc4
    .long exc5
    .long exc6
    .long exc7
    .long exc8
    .long exc9
    .long exc10
    .long exc11
    .long exc12
    .long exc13
    .long exc14
    .long exc15
    .long exc16
    .long exc17
    .long exc18
    .long exc19
    .long exc20
    .long exc21
    .long exc22
    .long exc23
    .long exc24
    .long exc25
    .long exc26
    .long exc27
    .long exc28
    .long exc29
    .long exc30
    .long exc31
