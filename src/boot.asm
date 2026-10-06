bits 32

KERNEL_VIRT_BASE equ 0xC0000000

section .boot
align 4
multiboot_header:
    dd 0x1BADB002                  ; magic
    dd 0x00000003                  ; flags: ALIGN (bit 0) + MEMINFO (bit 1)
    dd -(0x1BADB002 + 0x00000003)  ; checksum

global _start
extern kernel_main

_start:
    cli
    ; Preserve Multiboot registers passed by bootloader
    mov [(mb_magic_store - KERNEL_VIRT_BASE)], eax
    mov [(mb_info_store - KERNEL_VIRT_BASE)], ebx

    NUM_BOOT_TABLES equ 8 ; maps 32 MB (8 tables * 4MB)

    ; 1. Initialize boot_page_tables (maps physical 0 to 32MB)
    mov edi, (boot_page_tables - KERNEL_VIRT_BASE)
    xor eax, eax
    mov ecx, (1024 * NUM_BOOT_TABLES)
.fill_pt:
    mov ebx, eax
    or ebx, 0x003   ; PAGE_PRESENT | PAGE_WRITE
    mov [edi], ebx
    add eax, 4096
    add edi, 4
    loop .fill_pt

    ; 2. Link boot_page_tables into boot_page_dir:
    mov ecx, NUM_BOOT_TABLES
    xor edx, edx
.link_pde:
    mov eax, (boot_page_tables - KERNEL_VIRT_BASE)
    mov ebx, edx
    shl ebx, 12     ; edx * 4096
    add eax, ebx
    or eax, 0x003
    ; Identity mapping: entry edx
    mov [(boot_page_dir - KERNEL_VIRT_BASE) + edx * 4], eax
    ; Higher-half mapping: entry (768 + edx)
    mov [(boot_page_dir - KERNEL_VIRT_BASE) + (768 + edx) * 4], eax
    inc edx
    loop .link_pde

    ; 3. Load CR3 with physical address of boot_page_dir
    mov eax, (boot_page_dir - KERNEL_VIRT_BASE)
    mov cr3, eax

    ; 4. Enable Paging (CR0.PG = bit 31, CR0.PE = bit 0)
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    ; 5. Absolute far jump into Higher-Half virtual address space
    lea ecx, [.higher_half]
    jmp ecx

section .text
.higher_half:
    ; Now running at virtual address 0xC010xxxx!
    mov esp, stack_top
    lgdt [gdt_descriptor]
    jmp 0x08:.reload_cs

.reload_cs:
    mov cx, 0x10
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    mov ss, cx

    ; Enable x87 FPU for floating-point operations
    fninit

    ; Pass Multiboot arguments to kernel_main(magic, info)
    push dword [mb_info_store]        ; arg2: multiboot info pointer (physical)
    push dword [mb_magic_store]       ; arg1: multiboot magic (0x2BADB002)
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

section .data
align 4096
global boot_page_dir
boot_page_dir:
    times 1024 dd 0

align 4096
global boot_page_tables
boot_page_tables:
    times (1024 * NUM_BOOT_TABLES) dd 0

global mb_magic_store
global mb_info_store
mb_magic_store: dd 0
mb_info_store: dd 0

section .bss
align 16
resb 131072
stack_top:
