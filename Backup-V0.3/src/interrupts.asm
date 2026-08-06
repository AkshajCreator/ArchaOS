global idt_load
global irq1_stub
extern keyboard_isr

idt_load:
    mov eax, [esp+4]
    lidt [eax]
    sti
    ret

irq1_stub:
    pusha
    call keyboard_isr
    mov al, 0x20
    out 0x20, al
    popa
    iretd
