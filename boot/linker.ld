ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text ALIGN(4K) : { *(.multiboot) *(.text .text.*) }
    .rodata ALIGN(4K) : { *(.rodata .rodata.*) }

    /* everything below this address is mapped read-only by paging_init() */
    . = ALIGN(4K);
    __ro_end = .;

    .data ALIGN(4K) : { *(.data .data.*) }
    .bss ALIGN(4K) : { *(COMMON) *(.bss .bss.*) }
}
