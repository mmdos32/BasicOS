ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text ALIGN(4K) : { *(.multiboot) *(.text) }
    .rodata ALIGN(4K) : { *(.rodata*) }

    /* everything below this address is mapped read-only by paging_init() */
    . = ALIGN(4K);
    __ro_end = .;

    .data ALIGN(4K) : { *(.data) }
    .bss ALIGN(4K) : { *(COMMON) *(.bss) }
}
