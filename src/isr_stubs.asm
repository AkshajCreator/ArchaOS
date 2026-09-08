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
    push dword 0    ; dummy error code
    pusha
    push dword [esp + 36] ; EIP
    push dword [esp + 36] ; Error code
    push dword %1         ; ISR num
    call isr_handler
    add esp, 12
    popa
    add esp, 4
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
    pusha
    push dword [esp + 36] ; EIP
    push dword [esp + 36] ; Error code
    push dword %1         ; ISR num
    call isr_handler
    add esp, 12
    popa
    add esp, 4            ; discard error code
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


; Dedicated IRQ handlers
IRQ_STUB 0,  irq0_handler   ; PIT      -> vector 32
IRQ_STUB 1,  irq1_handler   ; keyboard -> vector 33
IRQ_STUB 8,  irq8_handler   ; RTC      -> vector 40
IRQ_STUB 12, irq12_handler  ; mouse    -> vector 44

; Default IRQ handler macro for unhandled hardware IRQs (vectors 34-39, 41-43, 45-47)
extern default_irq_handler
%macro IRQ_DEF 1
global _irq%1
_irq%1:
    cli
    pusha
    push dword %1
    call default_irq_handler
    add esp, 4
    popa
    iret
%endmacro

IRQ_DEF 2
IRQ_DEF 3
IRQ_DEF 4
IRQ_DEF 5
IRQ_DEF 6
IRQ_DEF 7
IRQ_DEF 9
IRQ_DEF 10
IRQ_DEF 11
IRQ_DEF 13
IRQ_DEF 14
IRQ_DEF 15


