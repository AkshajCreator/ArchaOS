; src/boot.asm

section .text
global _start
extern kernel_main

align 4
multiboot_header:
    dd 0x1BADB002                  ; magic
    dd 0x00000003                  ; flags: ALIGN (bit 0) + MEMINFO (bit 1)
    dd -(0x1BADB002 + 0x00000003)  ; checksum

_start:
    cli
    lgdt [gdt_descriptor]
    jmp 0x08:.reload_cs

.reload_cs:
    mov cx, 0x10
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    mov ss, cx
    mov esp, stack_top

    push 0
    popf

    ; Enable x87 FPU — required for float inference in ai.c
    fninit

    ; Pass Multiboot arguments to kernel_main(magic, info)
    push ebx        ; arg2: multiboot info pointer
    push eax        ; arg1: multiboot magic (0x2BADB002)

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

align 8
gdt_start:
    ; Null descriptor (0x00)
    dd 0x00000000
    dd 0x00000000

    ; 32-bit Kernel Code descriptor (0x08): Base 0, Limit 4GB, Ring 0, Exec/Read
    dw 0xFFFF       ; Limit 0..15
    dw 0x0000       ; Base 0..15
    db 0x00         ; Base 16..23
    db 0x9A         ; Access (Present, Ring 0, Code, Exec/Read)
    db 0xCF         ; Granularity (4KB pages, 32-bit pmode) + Limit 16..19
    db 0x00         ; Base 24..31

    ; 32-bit Kernel Data descriptor (0x10): Base 0, Limit 4GB, Ring 0, Read/Write
    dw 0xFFFF       ; Limit 0..15
    dw 0x0000       ; Base 0..15
    db 0x00         ; Base 16..23
    db 0x92         ; Access (Present, Ring 0, Data, Read/Write)
    db 0xCF         ; Granularity (4KB pages, 32-bit pmode) + Limit 16..19
    db 0x00         ; Base 24..31
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; GDT Limit
    dd gdt_start                ; GDT Base Address

section .bss
align 16
resb 131072
stack_top:
