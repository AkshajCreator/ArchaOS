ENTRY(_start)

SECTIONS
{
    /* Physical 1MB bootstrap location for Multiboot bootloader */
    . = 0x00100000;

    .boot : {
        *(.boot)
    }

    /* Transition to Higher-Half Virtual Address Space (3 GB base) */
    . += 0xC0000000;
    _kernel_start = .;

    .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000)
    {
        *(.text*)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000)
    {
        *(.rodata*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000)
    {
        *(.data*)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000)
    {
        *(.bss*)
        *(COMMON)
    }

    . = ALIGN(4K);
    _kernel_end = .;
}
