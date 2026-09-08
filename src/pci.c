// src/pci.c — PCI bus enumeration
#include "pci.h"
#include "serial.h"
#include "vga.h"
#include "kernel.h"  // for inb/outb
#include <stdint.h>
#include <stddef.h>

// Port I/O from kernel
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val)
{
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

// Device storage
static pci_device_t pci_devices[PCI_MAX_DEVICES];
static int pci_device_count = 0;

// Read 32-bit from PCI config space
uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

// Write 32-bit to PCI config space
void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// Check if device exists at bus:device:function
static int pci_check_device(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t vendor_device = pci_config_read(bus, device, function, PCI_VENDOR_ID);
    uint16_t vendor = (uint16_t)(vendor_device & 0xFFFF);
    return (vendor != 0xFFFF) && (vendor != 0x0000);
}

// Read device info
static void pci_read_device(pci_device_t *dev, uint8_t bus, uint8_t device, uint8_t function)
{
    dev->bus = bus;
    dev->device = device;
    dev->function = function;

    uint32_t vd = pci_config_read(bus, device, function, PCI_VENDOR_ID);
    dev->vendor_id = (uint16_t)(vd & 0xFFFF);
    dev->device_id = (uint16_t)(vd >> 16);

    uint32_t class_rev = pci_config_read(bus, device, function, PCI_REVISION_ID);
    dev->revision = (uint8_t)(class_rev & 0xFF);
    dev->prog_if = (uint8_t)((class_rev >> 8) & 0xFF);
    dev->subclass = (uint8_t)((class_rev >> 16) & 0xFF);
    dev->class_code = (uint8_t)((class_rev >> 24) & 0xFF);

    dev->header_type = pci_config_read8(bus, device, function, PCI_HEADER_TYPE);

    // Read BARs (Header type 0x01 bridge only has 2 BARs)
    int max_bars = ((dev->header_type & 0x7F) == 0x01) ? 2 : 6;
    for (int i = 0; i < 6; i++) {
        dev->bar[i] = (i < max_bars) ? pci_config_read(bus, device, function, PCI_BAR0 + i * 4) : 0;
    }

    dev->irq_line = pci_config_read8(bus, device, function, PCI_INT_LINE);
    dev->irq_pin  = pci_config_read8(bus, device, function, PCI_INT_PIN);
}

// Scan a single function
static void pci_scan_function(uint8_t bus, uint8_t device, uint8_t function)
{
    if (!pci_check_device(bus, device, function)) return;
    if (pci_device_count >= PCI_MAX_DEVICES) return;

    pci_read_device(&pci_devices[pci_device_count], bus, device, function);
    pci_device_count++;
}

// Scan a device (all functions)
static void pci_scan_device(uint8_t bus, uint8_t device)
{
    if (!pci_check_device(bus, device, 0)) return;

    pci_scan_function(bus, device, 0);

    // Multi-function device?
    uint8_t header_type = pci_config_read8(bus, device, 0, PCI_HEADER_TYPE);
    if (header_type & 0x80) {
        for (uint8_t func = 1; func < PCI_MAX_FUNCTIONS; func++) {
            if (pci_check_device(bus, device, func)) {
                pci_scan_function(bus, device, func);
            }
        }
    }
}

// Scan a bus (all devices)
static void pci_scan_bus(uint8_t bus)
{
    for (uint8_t device = 0; device < 32; device++) {
        pci_scan_device(bus, device);
    }
}

// Main scan function
int pci_scan(void)
{
    pci_device_count = 0;

    // Check if PCI mechanism #1 is supported (most modern systems)
    // Try bus 0 first
    pci_scan_bus(0);

    // Check for multiple buses via PCI-to-PCI bridges
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == PCI_CLASS_BRIDGE &&
            pci_devices[i].subclass == PCI_SUBCLASS_PCI_BRIDGE) {
            // This is a PCI-to-PCI bridge, secondary bus might have devices
            uint8_t sec_bus = pci_config_read8(pci_devices[i].bus, pci_devices[i].device, pci_devices[i].function, 0x19);
            uint8_t sub_bus = pci_config_read8(pci_devices[i].bus, pci_devices[i].device, pci_devices[i].function, 0x1A);
            if (sec_bus > 0 && sec_bus != 0xFF) {
                if (sub_bus >= sec_bus && sub_bus != 0xFF) {
                    for (uint16_t b = sec_bus; b <= sub_bus; b++) {
                        pci_scan_bus((uint8_t)b);
                    }
                } else {
                    pci_scan_bus(sec_bus);
                }
            }
        }
    }

    serial_printf(COM1_BASE, "PCI: Found %d device(s)\n", pci_device_count);
    return pci_device_count;
}

int pci_get_device_count(void)
{
    return pci_device_count;
}

pci_device_t *pci_get_device(int idx)
{
    if (idx < 0 || idx >= pci_device_count) return 0;
    return &pci_devices[idx];
}

pci_device_t *pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if)
{
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass &&
            (prog_if == 0xFF || pci_devices[i].prog_if == prog_if)) {
            return &pci_devices[i];
        }
    }
    return 0;
}

pci_device_t *pci_find_vendor_device(uint16_t vendor_id, uint16_t device_id)
{
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return 0;
}

uint32_t pci_get_bar(pci_device_t *dev, int bar_idx)
{
    if (bar_idx < 0 || bar_idx >= 6) return 0;
    return dev->bar[bar_idx];
}

int pci_bar_is_io(pci_device_t *dev, int bar_idx)
{
    if (bar_idx < 0 || bar_idx >= 6) return 0;
    return dev->bar[bar_idx] & PCI_BAR_IO;
}

int pci_bar_is_64bit(pci_device_t *dev, int bar_idx)
{
    if (bar_idx < 0 || bar_idx >= 6) return 0;
    return (dev->bar[bar_idx] & PCI_BAR_MEM_64) != 0;
}

uint32_t pci_bar_size(pci_device_t *dev, int bar_idx)
{
    if (!dev || bar_idx < 0 || bar_idx >= 6) return 0;
    int max_bars = ((dev->header_type & 0x7F) == 0x01) ? 2 : 6;
    if (bar_idx >= max_bars) return 0;

    uint32_t bar = dev->bar[bar_idx];
    if (bar == 0) return 0;
    int is_io = bar & PCI_BAR_IO;

    // Save command and disable memory/io decode temporarily
    uint16_t cmd = (uint16_t)pci_config_read(dev->bus, dev->device, dev->function, PCI_COMMAND);
    pci_config_write(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd & ~0x03);

    // Write all 1s to get size
    uint32_t original = bar;
    pci_config_write(dev->bus, dev->device, dev->function, PCI_BAR0 + bar_idx * 4, 0xFFFFFFFF);
    uint32_t size = pci_config_read(dev->bus, dev->device, dev->function, PCI_BAR0 + bar_idx * 4);
    pci_config_write(dev->bus, dev->device, dev->function, PCI_BAR0 + bar_idx * 4, original);

    // Restore command register
    pci_config_write(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);

    if (size == 0 || size == 0xFFFFFFFF) return 0;

    if (is_io) {
        return (~(size & 0xFFFFFFFC)) + 1;
    } else {
        return (~(size & 0xFFFFFFF0)) + 1;
    }
}

// Class name lookup
static const char *pci_class_name(uint8_t class_code, uint8_t subclass)
{
    switch (class_code) {
        case PCI_CLASS_STORAGE:
            if (subclass == 0x01) return "IDE Storage";
            if (subclass == 0x06) return "SATA Controller";
            if (subclass == 0x08) return "NVMe Controller";
            return "Storage Controller";
        case PCI_CLASS_NETWORK:   return "Network Controller";
        case PCI_CLASS_DISPLAY:   return "Display Controller";
        case PCI_CLASS_MULTIMEDIA:return "Multimedia Controller";
        case PCI_CLASS_MEMORY:    return "Memory Controller";
        case PCI_CLASS_BRIDGE:    return "Bridge Device";
        case PCI_CLASS_COMM:      return "Communication Device";
        case PCI_CLASS_INPUT:     return "Input Device";
        case PCI_CLASS_PROCESSOR: return "Processor";
        case PCI_CLASS_SERIAL:    return "Serial Bus Controller";
        default:                  return "Unknown Device";
    }
}

void pci_print_device(pci_device_t *dev)
{
    if (!dev) return;
    char buf[16];
    vga_print("PCI ");
    itoa(dev->bus, buf, 16); if (!buf[1]) vga_print("0"); vga_print(buf); vga_print(":");
    itoa(dev->device, buf, 16); if (!buf[1]) vga_print("0"); vga_print(buf); vga_print(".");
    itoa(dev->function, buf, 10); vga_print(buf);
    vga_print(" ["); vga_print(pci_class_name(dev->class_code, dev->subclass)); vga_print("]\n");

    vga_print("  Vendor:"); itoa(dev->vendor_id, buf, 16); vga_print(buf);
    vga_print(" Device:"); itoa(dev->device_id, buf, 16); vga_print(buf);
    vga_print(" Class:"); itoa(dev->class_code, buf, 16); vga_print(buf);
    vga_print(" Sub:"); itoa(dev->subclass, buf, 16); vga_print(buf); vga_print("\n");

    for (int i = 0; i < 6; i++) {
        if (dev->bar[i]) {
            vga_print("  BAR"); itoa(i, buf, 10); vga_print(buf); vga_print(": ");
            itoa(dev->bar[i], buf, 16); vga_print(buf);
            vga_print(" ("); vga_print(pci_bar_is_io(dev, i) ? "IO" : "MEM"); vga_print(")\n");
        }
    }
    if (dev->irq_line != 0xFF && dev->irq_line != 0) {
        vga_print("  IRQ: "); itoa(dev->irq_line, buf, 10); vga_print(buf); vga_print("\n");
    }

    serial_printf(COM1_BASE, "PCI %02x:%02x.%x: Vendor=%04x Device=%04x Class=%02x Sub=%02x ProgIF=%02x Rev=%02x [%s]\n",
                  dev->bus, dev->device, dev->function,
                  dev->vendor_id, dev->device_id,
                  dev->class_code, dev->subclass, dev->prog_if, dev->revision,
                  pci_class_name(dev->class_code, dev->subclass));
}

void pci_list_all(void)
{
    vga_print("\n=== PCI Device List ===\n");
    serial_puts(COM1_BASE, "\n=== PCI Device List ===\n");
    for (int i = 0; i < pci_device_count; i++) {
        pci_print_device(&pci_devices[i]);
    }
    if (pci_device_count == 0) {
        vga_print("No PCI devices found.\n");
        serial_puts(COM1_BASE, "No PCI devices found.\n");
    }
    vga_print("========================\n\n");
    serial_puts(COM1_BASE, "========================\n\n");
}