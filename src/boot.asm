; src/boot.asm

section .text
global _start
extern kernel_main

align 4
multiboot_header:
    dd 0x1BADB002
    dd 0x00000000
    dd -(0x1BADB002 + 0x00000000)

_start:
    mov esp, stack_top

    ; EBX = multiboot info pointer (passed by GRUB)
    ; EAX = multiboot magic (should be 0x2BADB002)
    ; Pass both to kernel_main as arguments
    push ebx        ; arg2: multiboot info pointer
    push eax        ; arg1: multiboot magic

    push 0
    popf

    ; Enable x87 FPU — required for float inference in ai.c
    fninit

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
resb 16384
stack_top:
