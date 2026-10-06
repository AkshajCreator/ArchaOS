ENTRY(_start)

SECTIONS {
    . = 0x08048000;

    .text ALIGN(4K) : {
        *(.text*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .init_array ALIGN(4) : {
        __init_array_start = .;
        *(.init_array*)
        *(.ctors*)
        __init_array_end = .;
    }

    .fini_array ALIGN(4) : {
        __fini_array_start = .;
        *(.fini_array*)
        *(.dtors*)
        __fini_array_end = .;
    }

    .data ALIGN(4K) : {
        *(.data*)
    }

    .bss ALIGN(4K) : {
        *(.bss*)
        *(COMMON)
    }
}
