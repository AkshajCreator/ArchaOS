; src/isr_stubs.asm
; Pure NASM stubs — no GCC naked function tricks

bits 32
section .text

extern isr_handler
extern irq0_handler
extern irq1_handler
extern irq8_handler
extern irq12_handler

; ============================================================
; MACRO: exception stub (no error code)
; ============================================================
%macro ISR_NOERR 1
global _isr%1
_isr%1:
    cli
    pusha
    push dword %1
    call isr_handler
    add esp, 4
    popa
    iret
%endmacro

; ============================================================
; MACRO: IRQ stub
; ============================================================
%macro IRQ_STUB 2
global _irq%1
_irq%1:
    cli
    pusha
    call %2
    popa
    sti
    iret
%endmacro

; ============================================================
; MACRO: exception stub (WITH error code — CPU pushes it first)
; Exceptions: 8, 10, 11, 12, 13, 14, 17
; ============================================================
%macro ISR_ERR 1
global _isr%1
_isr%1:
    cli
    add esp, 4      ; discard the hardware-pushed error code
    pusha
    push dword %1
    call isr_handler
    add esp, 4
    popa
    iret
%endmacro

; CPU exceptions 0-19
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8    ; Double Fault          (pushes error code)
ISR_NOERR 9
ISR_ERR   10   ; Invalid TSS           (pushes error code)
ISR_ERR   11   ; Segment Not Present   (pushes error code)
ISR_ERR   12   ; Stack Fault           (pushes error code)
ISR_ERR   13   ; General Protection    (pushes error code)
ISR_ERR   14   ; Page Fault            (pushes error code)
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17   ; Alignment Check       (pushes error code)
ISR_NOERR 18
ISR_NOERR 19


; IRQ handlers
IRQ_STUB 0,  irq0_handler   ; PIT      -> vector 32
IRQ_STUB 1,  irq1_handler   ; keyboard -> vector 33
IRQ_STUB 8,  irq8_handler   ; RTC      -> vector 40
IRQ_STUB 12, irq12_handler  ; mouse    -> vector 44

