// src/serial.h — 16550 UART driver
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

// Standard COM port base addresses
#define COM1_BASE 0x3F8
#define COM2_BASE 0x2F8
#define COM3_BASE 0x3E8
#define COM4_BASE 0x2E8

// 16550 Register offsets
#define UART_RBR 0   // Receive Buffer Register (read)
#define UART_THR 0   // Transmit Holding Register (write)
#define UART_IER 1   // Interrupt Enable Register
#define UART_IIR 2   // Interrupt Identification Register (read)
#define UART_FCR 2   // FIFO Control Register (write)
#define UART_LCR 3   // Line Control Register
#define UART_MCR 4   // Modem Control Register
#define UART_LSR 5   // Line Status Register
#define UART_MSR 6   // Modem Status Register
#define UART_SCR 7   // Scratch Register
#define UART_DLL 0   // Divisor Latch Low (when DLAB=1)
#define UART_DLM 1   // Divisor Latch High (when DLAB=1)

// Line Control Register bits
#define LCR_DLAB  0x80  // Divisor Latch Access Bit
#define LCR_8N1   0x03  // 8 data, no parity, 1 stop

// Line Status Register bits
#define LSR_DR    0x01  // Data Ready
#define LSR_OE    0x02  // Overrun Error
#define LSR_PE    0x04  // Parity Error
#define LSR_FE    0x08  // Framing Error
#define LSR_BI    0x10  // Break Interrupt
#define LSR_THRE  0x20  // Transmitter Holding Register Empty
#define LSR_TEMT  0x40  // Transmitter Empty
#define LSR_FIFOE 0x80  // FIFO Error

// FIFO Control Register bits
#define FCR_FIFO_EN   0x01  // Enable FIFOs
#define FCR_RX_CLR    0x02  // Clear RX FIFO
#define FCR_TX_CLR    0x04  // Clear TX FIFO
#define FCR_DMA_MODE  0x08  // DMA mode select
#define FCR_RX_TRIG_1  0x00  // RX trigger at 1 byte
#define FCR_RX_TRIG_4  0x40  // RX trigger at 4 bytes
#define FCR_RX_TRIG_8  0x80  // RX trigger at 8 bytes
#define FCR_RX_TRIG_14 0xC0  // RX trigger at 14 bytes

// Modem Control Register bits
#define MCR_DTR  0x01  // Data Terminal Ready
#define MCR_RTS  0x02  // Request to Send
#define MCR_OUT1 0x04  // Output 1
#define MCR_OUT2 0x08  // Output 2 (enables interrupts in PC)
#define MCR_LOOP 0x10  // Loopback mode

// Initialize serial port (base I/O address, baud rate)
void serial_init(uint16_t base, uint32_t baud);

// Send a single character (blocking)
void serial_putc(uint16_t base, char c);

// Send null-terminated string (blocking)
void serial_puts(uint16_t base, const char *s);

// Send formatted string (blocking) - like printf but to serial
void serial_printf(uint16_t base, const char *fmt, ...);

// Check if data is available (non-blocking)
int serial_data_ready(uint16_t base);

// Read a character (blocking)
char serial_getc(uint16_t base);

// Read a character (non-blocking, returns 0 if none)
char serial_try_getc(uint16_t base);

#endif