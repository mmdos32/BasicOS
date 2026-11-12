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

    /* ring-3 code + stack (syscall test): paging_init() maps these pages
     * user-accessible, so ring 3 can fetch and push there. Kept in two
     * sections so no LOAD segment ends up RWX. */
    .user_text ALIGN(4K) : {
        __user_start = .;
        *(.user_text .user_text.*)
        . = ALIGN(4K);
    }
    .user_bss ALIGN(4K) : {
        *(.user_bss .user_bss.*)
        . = ALIGN(4K);
        __user_end = .;
    }
}
