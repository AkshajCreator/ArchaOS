// src/ata.c — ATA/IDE PIO driver implementation
#include "ata.h"
#include "serial.h"
#include "vga.h"
#include "kernel.h"
#include <stdint.h>
#include <stddef.h>

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

static inline void outw(uint16_t port, uint16_t val)
{
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void insw(uint16_t port, void *buf, uint32_t count)
{
    uint16_t *dest = (uint16_t *)buf;
    asm volatile("rep insw" : "+D"(dest), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void *buf, uint32_t count)
{
    const uint16_t *src = (const uint16_t *)buf;
    asm volatile("rep outsw" : "+S"(src), "+c"(count) : "d"(port) : "memory");
}

// ATA device storage
static ata_device_t ata_devices[ATA_MAX_DEVICES];
static int ata_device_count = 0;

// Controller info - placeholder for PCI controller detection
// static ata_controller_t ata_controller;  // TODO: use when PCI IDE detection implemented

// Wait for BSY to clear
static int ata_wait_ready(uint16_t base, uint32_t timeout)
{
    while (timeout--) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (!(status & ATA_STATUS_BSY)) {
            return (status & ATA_STATUS_ERR) ? -1 : 0;
        }
        // Small delay
        for (volatile int i = 0; i < 1000; i++);
    }
    return -1;  // Timeout
}

// Wait for DRQ (data request)
static int ata_wait_drq(uint16_t base, uint32_t timeout)
{
    while (timeout--) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (status & ATA_STATUS_ERR) return -1;
        if (status & ATA_STATUS_DRQ) return 0;
        for (volatile int i = 0; i < 1000; i++);
    }
    return -1;
}

// Select device (master/slave)
static void ata_select_device(uint16_t base, uint8_t device)
{
    outb(base + ATA_REG_DEVICE, 0xE0 | (device << 4));
    // Wait for device selection
    for (volatile int i = 0; i < 400; i++);
}

// Read identify data into buffer
static int ata_do_identify(ata_device_t *dev, uint16_t *buffer)
{
    uint16_t base = dev->base_port;

    ata_select_device(base, dev->device);
    if (ata_wait_ready(base, 100000) < 0) return -1;

    outb(base + ATA_REG_FEATURES, 0);
    outb(base + ATA_REG_SECTOR_CNT, 0);
    outb(base + ATA_REG_LBA_LOW, 0);
    outb(base + ATA_REG_LBA_MID, 0);
    outb(base + ATA_REG_LBA_HIGH, 0);
    outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    // Wait for BSY to clear
    if (ata_wait_ready(base, 100000) < 0) return -1;

    uint8_t status = inb(base + ATA_REG_STATUS);
    if (status & ATA_STATUS_ERR) return -1;

    // Check if it's ATAPI (would have different signature)
    uint8_t lba_mid = inb(base + ATA_REG_LBA_MID);
    uint8_t lba_high = inb(base + ATA_REG_LBA_HIGH);
    if (lba_mid == ATAPI_SIG_LBA_MID && lba_high == ATAPI_SIG_LBA_HIGH) {
        dev->type = ATA_TYPE_ATAPI;
        // Don't read identify for ATAPI here
        return 0;
    }

    dev->type = ATA_TYPE_ATA;

    // Read 256 words (512 bytes) of identify data
    insw(base + ATA_REG_DATA, buffer, 256);

    return 0;
}

// Parse identify data into device struct
static void ata_parse_identify(ata_device_t *dev, const uint16_t *identify)
{
    // Model (words 27-46, 40 chars)
    for (int i = 0; i < 20; i++) {
        uint16_t w = identify[27 + i];
        dev->model[i * 2] = w >> 8;
        dev->model[i * 2 + 1] = w & 0xFF;
    }
    dev->model[40] = '\0';

    // Serial (words 10-19, 20 chars)
    for (int i = 0; i < 10; i++) {
        uint16_t w = identify[10 + i];
        dev->serial[i * 2] = w >> 8;
        dev->serial[i * 2 + 1] = w & 0xFF;
    }
    dev->serial[20] = '\0';

    // Firmware (words 23-26, 8 chars)
    for (int i = 0; i < 4; i++) {
        uint16_t w = identify[23 + i];
        dev->firmware[i * 2] = w >> 8;
        dev->firmware[i * 2 + 1] = w & 0xFF;
    }
    dev->firmware[8] = '\0';

    // Capabilities (word 49)
    dev->capabilities = identify[49];

    // Total sectors LBA28 (words 60-61)
    dev->sectors = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);

    // Command sets supported (words 82-84)
    dev->command_sets = identify[82];

    // LBA48 support (word 83 bit 10)
    if (identify[83] & (1 << 10)) {
        // 48-bit LBA (words 100-103)
        dev->sectors_lba48 = (uint64_t)identify[100] |
                            ((uint64_t)identify[101] << 16) |
                            ((uint64_t)identify[102] << 32) |
                            ((uint64_t)identify[103] << 48);
    } else {
        dev->sectors_lba48 = dev->sectors;
    }

    // Trim strings
    for (int i = 39; i >= 0; i--) {
        if (dev->model[i] == ' ') dev->model[i] = '\0';
        else break;
    }
    for (int i = 19; i >= 0; i--) {
        if (dev->serial[i] == ' ') dev->serial[i] = '\0';
        else break;
    }
    for (int i = 7; i >= 0; i--) {
        if (dev->firmware[i] == ' ') dev->firmware[i] = '\0';
        else break;
    }
}

// Probe a single device
static int ata_probe_device(uint8_t channel, uint8_t device)
{
    uint16_t base = (channel == 0) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
    uint16_t ctrl = (channel == 0) ? 0x3F6 : 0x376;

    if (ata_device_count >= ATA_MAX_DEVICES) return 0;

    ata_device_t *dev = &ata_devices[ata_device_count];
    // Manual memset since we can't use string.h
    uint8_t *p = (uint8_t *)dev;
    for (size_t i = 0; i < sizeof(ata_device_t); i++) p[i] = 0;

    dev->base_port = base;
    dev->ctrl_port = ctrl;
    dev->device = device;
    dev->channel = channel;

    // Select device and check if present
    ata_select_device(base, device);

    // Small delay
    for (volatile int i = 0; i < 1000; i++);

    uint8_t status = inb(base + ATA_REG_STATUS);
    if (status == 0xFF || status == 0x00 || status == 0x7F) return 0;  // No legacy IDE controller
    if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRDY)) == ATA_STATUS_DRDY) {
        // Device might be present but not ready yet, continue
    } else if (status & ATA_STATUS_BSY) {
        // Device is busy, might not be present
        // Wait a bit more
        for (volatile int i = 0; i < 100000; i++);
        status = inb(base + ATA_REG_STATUS);
        if (status & ATA_STATUS_BSY) return 0;  // Still busy, no device
    }

    // Try identify
    uint16_t identify[256];
    if (ata_do_identify(dev, identify) < 0) {
        // Not an ATA device
        return 0;
    }

    if (dev->type == ATA_TYPE_ATAPI) {
        // Skip ATAPI for now
        serial_printf(COM1_BASE, "ATA: Found ATAPI device on channel %d device %d\n", channel, device);
        return 0;
    }

    ata_parse_identify(dev, identify);

    // Verify it's actually a valid device (model should not be empty for real devices)
    if (dev->model[0] == '\0' && dev->serial[0] == '\0' && dev->firmware[0] == '\0') {
        return 0;
    }

    dev->present = 1;
    ata_device_count++;

    serial_printf(COM1_BASE, "ATA: Found %s (%s, %u sectors)\n", dev->model, dev->serial, dev->sectors);

    return 1;
}

// Find ATA controller via PCI
ata_controller_t *ata_find_controller(void)
{
    extern int pci_get_device_count(void);
    extern void *pci_get_device(int idx);

    for (int i = 0; i < pci_get_device_count(); i++) {
        // We need access to pci_device_t fields - using extern functions
        // For now, just use legacy ports
        // TODO: parse PCI devices properly
    }
    return 0;
}

// Public API

void ata_init(void)
{
    serial_puts(COM1_BASE, "ATA: Initializing...\n");

    // Try to find PCI IDE controller
    // For now, use legacy ports

    // Probe all 4 possible devices
    ata_probe_device(0, 0);  // Primary master
    ata_probe_device(0, 1);  // Primary slave
    ata_probe_device(1, 0);  // Secondary master
    ata_probe_device(1, 1);  // Secondary slave

    serial_printf(COM1_BASE, "ATA: Found %d device(s)\n", ata_device_count);
}

int ata_get_device_count(void)
{
    return ata_device_count;
}

ata_device_t *ata_get_device(int idx)
{
    if (idx < 0 || idx >= ata_device_count) return 0;
    return &ata_devices[idx];
}

int ata_read_sectors(ata_device_t *dev, uint32_t lba, uint8_t count, void *buffer)
{
    if (!dev || !dev->present || dev->type != ATA_TYPE_ATA) return -1;
    if (count == 0) return -1;
    if (lba > 0x0FFFFFFF || (lba + count) > dev->sectors) return -1;

    uint16_t base = dev->base_port;

    // Wait for drive ready
    if (ata_wait_ready(base, 100000) < 0) return -1;

    // Select device with LBA
    outb(base + ATA_REG_DEVICE, 0xE0 | (dev->device << 4) | ((lba >> 24) & 0x0F));
    for (int i = 0; i < 4; i++) inb(dev->ctrl_port + ATA_REG_ALT_STATUS);

    // Send command parameters
    outb(base + ATA_REG_FEATURES, 0);
    outb(base + ATA_REG_SECTOR_CNT, count);
    outb(base + ATA_REG_LBA_LOW, lba & 0xFF);
    outb(base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(base + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(base + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    // Read each sector
    uint16_t *buf = (uint16_t *)buffer;
    for (int s = 0; s < count; s++) {
        if (ata_wait_drq(base, 100000) < 0) return -1;
        insw(base + ATA_REG_DATA, buf, 256);  // 256 words = 512 bytes
        buf += 256;
    }

    // Wait for command completion
    if (ata_wait_ready(base, 100000) < 0) return -1;

    return 0;
}

int ata_write_sectors(ata_device_t *dev, uint32_t lba, uint8_t count, const void *buffer)
{
    if (!dev || !dev->present || dev->type != ATA_TYPE_ATA) return -1;
    if (count == 0) return -1;
    if (lba > 0x0FFFFFFF || (lba + count) > dev->sectors) return -1;

    uint16_t base = dev->base_port;

    // Wait for drive ready
    if (ata_wait_ready(base, 100000) < 0) return -1;

    // Select device with LBA
    outb(base + ATA_REG_DEVICE, 0xE0 | (dev->device << 4) | ((lba >> 24) & 0x0F));
    for (int i = 0; i < 4; i++) inb(dev->ctrl_port + ATA_REG_ALT_STATUS);

    // Send command parameters
    outb(base + ATA_REG_FEATURES, 0);
    outb(base + ATA_REG_SECTOR_CNT, count);
    outb(base + ATA_REG_LBA_LOW, lba & 0xFF);
    outb(base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(base + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

    // Write each sector
    const uint16_t *buf = (const uint16_t *)buffer;
    for (int s = 0; s < count; s++) {
        if (ata_wait_drq(base, 100000) < 0) return -1;
        outsw(base + ATA_REG_DATA, buf, 256);
        buf += 256;
    }

    // Flush cache
    outb(base + ATA_REG_COMMAND, ATA_CMD_FLUSH_CACHE);
    if (ata_wait_ready(base, 100000) < 0) return -1;

    return 0;
}

int ata_identify(ata_device_t *dev)
{
    if (!dev || !dev->present) return -1;

    uint16_t identify[256];
    if (ata_do_identify(dev, identify) < 0) return -1;
    ata_parse_identify(dev, identify);

    return 0;
}

void ata_list_all(void)
{
    vga_print("\n=== ATA Devices ===\n");
    serial_puts(COM1_BASE, "\n=== ATA Devices ===\n");
    for (int i = 0; i < ata_device_count; i++) {
        ata_device_t *dev = &ata_devices[i];
        char buf[16];
        vga_print("ATA "); itoa(i, buf, 10); vga_print(buf); vga_print(": ");
        vga_print(dev->model[0] ? dev->model : "Generic ATA Drive"); vga_print("\n");
        vga_print("  Channel: "); vga_print(dev->channel == 0 ? "Primary" : "Secondary");
        vga_print(", "); vga_print(dev->device == 0 ? "Master" : "Slave"); vga_print("\n");
        vga_print("  Sectors: "); itoa(dev->sectors, buf, 10); vga_print(buf);
        vga_print(" ("); itoa(dev->sectors / 2048, buf, 10); vga_print(buf); vga_print(" MB)\n");

        serial_printf(COM1_BASE, "ATA %d: %s %s %s\n",
                      i, dev->model, dev->serial, dev->firmware);
        serial_printf(COM1_BASE, "  Channel: %s, Device: %s\n",
                      dev->channel == 0 ? "Primary" : "Secondary",
                      dev->device == 0 ? "Master" : "Slave");
        serial_printf(COM1_BASE, "  Sectors: %u (LBA48: %u)\n", dev->sectors, (uint32_t)dev->sectors_lba48);
        serial_printf(COM1_BASE, "  Size: %u MB\n", (dev->sectors / 2048));
    }
    if (ata_device_count == 0) {
        vga_print("No ATA drives detected.\n");
        serial_puts(COM1_BASE, "No ATA drives detected.\n");
    }
    vga_print("===================\n\n");
    serial_puts(COM1_BASE, "===================\n\n");
}


// Read identify data word
uint16_t ata_identify_word(const uint16_t *identify, int word)
{
    if (word < 0 || word >= 256) return 0;
    return identify[word];
}