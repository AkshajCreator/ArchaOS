// src/serial.c — 16550 UART driver implementation
#include "serial.h"
#include <stdint.h>
#include <stdarg.h>

// Port I/O
static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Helper: wait for transmit buffer to be empty
static void serial_wait_thre(uint16_t base)
{
    uint32_t timeout = 100000;
    while (!(inb(base + UART_LSR) & LSR_THRE) && --timeout)
        ;
}

// Initialize serial port at given base address and baud rate
void serial_init(uint16_t base, uint32_t baud)
{
    // Disable all interrupts
    outb(base + UART_IER, 0x00);

    // Set DLAB=1 to access divisor latches
    outb(base + UART_LCR, LCR_DLAB);

    // Calculate divisor: 115200 / baud
    if (baud == 0) baud = 115200;
    uint32_t div = 115200 / baud;
    if (div == 0) div = 1;
    uint16_t divisor = (uint16_t)div;
    outb(base + UART_DLL, divisor & 0xFF);
    outb(base + UART_DLM, (divisor >> 8) & 0xFF);

    // 8 data bits, no parity, 1 stop bit, DLAB=0
    outb(base + UART_LCR, LCR_8N1);

    // Enable FIFOs, clear them, set 14-byte trigger
    outb(base + UART_FCR, FCR_FIFO_EN | FCR_RX_CLR | FCR_TX_CLR | FCR_RX_TRIG_14);

    // Enable DTR, RTS, OUT2 (required for PC interrupt routing)
    outb(base + UART_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

    // Clear any pending data
    (void)inb(base + UART_RBR);
}

// Send a character (blocking)
void serial_putc(uint16_t base, char c)
{
    serial_wait_thre(base);
    outb(base + UART_THR, (uint8_t)c);
}

// Send a string
void serial_puts(uint16_t base, const char *s)
{
    while (*s) {
        if (*s == '\n') {
            serial_putc(base, '\r');
        }
        serial_putc(base, *s++);
    }
}

// ---- Minimal printf implementation ----
static void serial_put_hex(uint16_t base, uint32_t val, int digits)
{
    if (digits <= 0) {
        // No zero padding: output minimum digits needed (like standard printf)
        if (val == 0) {
            serial_putc(base, '0');
            return;
        }
        char buf[9];
        int i = 0;
        while (val > 0) {
            uint8_t nibble = val & 0xF;
            buf[i++] = (nibble < 10) ? '0' + nibble : 'A' + nibble - 10;
            val >>= 4;
        }
        while (i--) serial_putc(base, buf[i]);
        return;
    }
    if (digits > 8) digits = 8;
    // Zero-padded output
    char buf[9];
    int i = 0;
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        uint8_t nibble = (val >> shift) & 0xF;
        buf[i++] = (nibble < 10) ? '0' + nibble : 'A' + nibble - 10;
    }
    buf[digits] = '\0';
    serial_puts(base, buf);
}

static void serial_put_dec(uint16_t base, int val)
{
    if (val == 0) {
        serial_putc(base, '0');
        return;
    }
    uint32_t uval;
    if (val < 0) {
        serial_putc(base, '-');
        uval = (uint32_t)(-(int64_t)val);
    } else {
        uval = (uint32_t)val;
    }
    char buf[12];
    int i = 0;
    while (uval > 0) {
        buf[i++] = '0' + (uval % 10);
        uval /= 10;
    }
    while (i--) serial_putc(base, buf[i]);
}

static void serial_put_udec(uint16_t base, uint32_t val)
{
    if (val == 0) {
        serial_putc(base, '0');
        return;
    }
    char buf[12];
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i--) serial_putc(base, buf[i]);
}

static void serial_put_ptr(uint16_t base, uint32_t val)
{
    serial_puts(base, "0x");
    serial_put_hex(base, val, 8);
}

// Formatted output (subset of printf: %c %s %d %u %x %p %% )
// Supports basic width: %02x %04x %08x
void serial_printf(uint16_t base, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            int width = 0;
            int zero_pad = 0;

            // Parse optional width (e.g. %02x, %4d)
            if (*fmt == '0') {
                zero_pad = 1;
                (void)zero_pad;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            switch (*fmt) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    serial_putc(base, c);
                    break;
                }
                case 's': {
                    const char *s = va_arg(args, const char *);
                    serial_puts(base, s ? s : "(null)");
                    break;
                }
                case 'd':
                case 'i': {
                    int d = va_arg(args, int);
                    serial_put_dec(base, d);
                    break;
                }
                case 'u': {
                    uint32_t u = va_arg(args, uint32_t);
                    serial_put_udec(base, u);
                    break;
                }
                case 'x': {
                    uint32_t x = va_arg(args, uint32_t);
                    if (width > 0) {
                        serial_put_hex(base, x, width);
                    } else {
                        // Default: no zero padding (like standard printf)
                        serial_put_hex(base, x, 0);
                    }
                    break;
                }
                case 'p': {
                    uint32_t p = va_arg(args, uint32_t);
                    serial_put_ptr(base, p);
                    break;
                }
                case '%':
                    serial_putc(base, '%');
                    break;
                default:
                    serial_putc(base, '%');
                    serial_putc(base, *fmt);
                    break;
            }
        } else {
            if (*fmt == '\n') serial_putc(base, '\r');
            serial_putc(base, *fmt);
        }
        fmt++;
    }
    va_end(args);
}

// Check if data is ready
int serial_data_ready(uint16_t base)
{
    return inb(base + UART_LSR) & LSR_DR;
}

// Read character (blocking with safety timeout)
char serial_getc(uint16_t base)
{
    uint32_t timeout = 1000000;
    while (!serial_data_ready(base) && --timeout)
        asm volatile("pause");
    if (!timeout) return 0;
    return (char)inb(base + UART_RBR);
}

// Read character (non-blocking)
char serial_try_getc(uint16_t base)
{
    if (!serial_data_ready(base)) return 0;
    return (char)inb(base + UART_RBR);
}