ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* Read + Execute (4|1) */
    rodata PT_LOAD FLAGS(4); /* Read-only (4) */
    data PT_LOAD FLAGS(6);   /* Read + Write (4|2) */
}

SECTIONS
{
    . = 1M;

    .text :
    {
        *(.text*)
    } :text

    .rodata :
    {
        *(.rodata*)
    } :rodata

    .data :
    {
        *(.data*)
    } :data

    .bss :
    {
        *(.bss*)
        *(COMMON)
    } :data
}
