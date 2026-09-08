// src/ata.h — ATA/IDE PIO driver interface
#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>

// ATA I/O port registers (primary channel: 0x1F0-0x1F7, secondary: 0x170-0x177)
#define ATA_PRIMARY_BASE   0x1F0
#define ATA_SECONDARY_BASE 0x170

// ATA register offsets
#define ATA_REG_DATA       0x00  // Data port (16-bit)
#define ATA_REG_FEATURES   0x01  // Features/Error register
#define ATA_REG_SECTOR_CNT 0x02  // Sector count
#define ATA_REG_LBA_LOW    0x03  // LBA low (bits 0-7)
#define ATA_REG_LBA_MID    0x04  // LBA mid (bits 8-15)
#define ATA_REG_LBA_HIGH   0x05  // LBA high (bits 16-23)
#define ATA_REG_DEVICE     0x06  // Device/Head register
#define ATA_REG_STATUS     0x07  // Status/Command register (read=status, write=command)
#define ATA_REG_COMMAND    ATA_REG_STATUS  // Command register (write-only, same port as status)
#define ATA_REG_ALT_STATUS 0x0E  // Alternate status (control block)
#define ATA_REG_CONTROL    0x0E  // Device control (write to control block)

// ATA commands
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_READ_DMA        0xC8
#define ATA_CMD_WRITE_DMA       0xCA
#define ATA_CMD_FLUSH_CACHE     0xE7

// ATA status register bits
#define ATA_STATUS_BSY  0x80  // Busy
#define ATA_STATUS_DRDY 0x40  // Device ready
#define ATA_STATUS_DF   0x20  // Device fault
#define ATA_STATUS_DRQ  0x08  // Data request ready
#define ATA_STATUS_CORR 0x04  // Corrected data
#define ATA_STATUS_IDX  0x02  // Index
#define ATA_STATUS_ERR  0x01  // Error

// ATA device register bits
#define ATA_DEVICE_LBA  0x40  // LBA mode (vs CHS)
#define ATA_DEVICE_DEV  0x10  // Device select (0=master, 1=slave)

// ATA control register bits
#define ATA_CTRL_NIEN 0x02  // Disable interrupts
#define ATA_CTRL_SRST 0x04  // Software reset

// ATAPI signature
#define ATAPI_SIG_LBA_MID 0x14
#define ATAPI_SIG_LBA_HIGH 0xEB

// Maximum ATA devices (2 channels × 2 devices)
#define ATA_MAX_DEVICES 4

// ATA device types
typedef enum {
    ATA_TYPE_NONE = 0,
    ATA_TYPE_ATA  = 1,   // ATA hard disk
    ATA_TYPE_ATAPI = 2   // ATAPI (CD-ROM, etc.)
} ata_device_type_t;

// ATA device info
typedef struct {
    ata_device_type_t type;
    uint16_t base_port;       // Command block base (0x1F0 or 0x170)
    uint16_t ctrl_port;       // Control block base (0x3F6 or 0x376)
    uint8_t  device;          // 0=master, 1=slave
    uint8_t  channel;         // 0=primary, 1=secondary

    char model[41];
    char serial[21];
    char firmware[9];

    uint32_t sectors;         // Total sectors (LBA28)
    uint64_t sectors_lba48;   // Total sectors (LBA48)
    uint16_t capabilities;    // Word 49
    uint16_t command_sets;    // Word 82/83/84

    int present;
} ata_device_t;

// Controller info (for PCI enumeration)
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint32_t bar0;  // Primary command block
    uint32_t bar1;  // Primary control block
    uint32_t bar2;  // Secondary command block
    uint32_t bar3;  // Secondary control block
    uint32_t bar4;  // Bus master IDE
    int uses_native_pci;  // 1 if using native PCI mode, 0 if legacy
} ata_controller_t;

// Initialize ATA subsystem (detect controllers and devices)
void ata_init(void);

// Get device count
int ata_get_device_count(void);

// Get device by index
ata_device_t *ata_get_device(int idx);

// Read sectors (LBA28)
int ata_read_sectors(ata_device_t *dev, uint32_t lba, uint8_t count, void *buffer);

// Write sectors (LBA28)
int ata_write_sectors(ata_device_t *dev, uint32_t lba, uint8_t count, const void *buffer);

// Identify device (read IDENTIFY DEVICE data)
int ata_identify(ata_device_t *dev);

// Print all detected devices
void ata_list_all(void);

// Find ATA controller via PCI
ata_controller_t *ata_find_controller(void);

// Read 16-bit word from identify data
uint16_t ata_identify_word(const uint16_t *identify, int word);

#endif // ATA_H