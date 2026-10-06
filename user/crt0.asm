[bits 32]
section .text

global _start
extern main
extern exit
extern __libc_init_array

_start:
    ; Terminate stack frame backtrace by zeroing ebp
    xor ebp, ebp

    ; Pop argc and argv pointer passed by the kernel
    pop eax         ; eax = argc
    mov ebx, esp    ; ebx = argv (array of char* pointers)

    ; 16-byte stack alignment for GCC ABI
    and esp, 0xFFFFFFF0
    sub esp, 8      ; 8 bytes padding + 8 bytes parameters = 16 bytes aligned
    push ebx        ; argv
    push eax        ; argc

    ; Run static constructors / initialization functions
    call __libc_init_array

    call main
    add esp, 16


    ; Invoke exit(eax) with return value of main
    push eax
    call exit

    ; Infinite halt fallback if exit ever returns
.hang:
    hlt
    jmp .hang
