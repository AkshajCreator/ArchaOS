// src/pci.h — PCI bus enumeration
#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI config space offsets
#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_REVISION_ID    0x08
#define PCI_PROG_IF        0x09
#define PCI_SUBCLASS       0x0A
#define PCI_CLASS          0x0B
#define PCI_CACHE_LINE     0x0C
#define PCI_LATENCY        0x0D
#define PCI_HEADER_TYPE    0x0E
#define PCI_BIST           0x0F
#define PCI_BAR0           0x10
#define PCI_BAR1           0x14
#define PCI_BAR2           0x18
#define PCI_BAR3           0x1C
#define PCI_BAR4           0x20
#define PCI_BAR5           0x24
#define PCI_CARDBUS_CIS    0x28
#define PCI_SUBSYS_VENDOR  0x2C
#define PCI_SUBSYS_ID      0x2E
#define PCI_EXP_ROM        0x30
#define PCI_CAP_PTR        0x34
#define PCI_INT_LINE       0x3C
#define PCI_INT_PIN        0x3D
#define PCI_MIN_GNT        0x3E
#define PCI_MAX_LAT        0x3F

// Base Address Register types
#define PCI_BAR_IO         0x01
#define PCI_BAR_MEM        0x00
#define PCI_BAR_MEM_64     0x04
#define PCI_BAR_PREFETCH   0x08

// Common class codes
#define PCI_CLASS_STORAGE      0x01
#define PCI_CLASS_NETWORK      0x02
#define PCI_CLASS_DISPLAY      0x03
#define PCI_CLASS_MULTIMEDIA   0x04
#define PCI_CLASS_MEMORY       0x05
#define PCI_CLASS_BRIDGE       0x06
#define PCI_CLASS_COMM         0x07
#define PCI_CLASS_BASE_PERIPH  0x08
#define PCI_CLASS_INPUT        0x09
#define PCI_CLASS_DOCKING      0x0A
#define PCI_CLASS_PROCESSOR    0x0B
#define PCI_CLASS_SERIAL       0x0C
#define PCI_CLASS_WIRELESS     0x0D
#define PCI_CLASS_INTELLIGENT  0x0E
#define PCI_CLASS_SATELLITE    0x0F
#define PCI_CLASS_CRYPTO       0x10
#define PCI_CLASS_DSP          0x11

// Storage subclasses
#define PCI_SUBCLASS_IDE       0x01
#define PCI_SUBCLASS_SATA      0x06
#define PCI_SUBCLASS_NVME      0x08

// Bridge subclasses
#define PCI_SUBCLASS_PCI_BRIDGE 0x04

// Programming interfaces for SATA (AHCI)
#define PCI_PROG_IF_AHCI        0x01

// Max devices
#define PCI_MAX_DEVICES        32
#define PCI_MAX_FUNCTIONS      8

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
    uint32_t bar[6];
    uint8_t  irq_line;
    uint8_t  irq_pin;
} pci_device_t;

// PCI config space access
uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

// Read 16-bit and 8-bit helpers
static inline uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    return (uint16_t)(pci_config_read(bus, device, function, offset & ~3) >> ((offset & 3) * 8));
}

static inline uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    return (uint8_t)(pci_config_read(bus, device, function, offset & ~3) >> ((offset & 3) * 8));
}

// Enumeration
int pci_scan(void);                    // Scan all buses/devices/functions
int pci_get_device_count(void);        // Get count of found devices
pci_device_t *pci_get_device(int idx); // Get device by index

// Find by class/subclass
pci_device_t *pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if);
pci_device_t *pci_find_vendor_device(uint16_t vendor_id, uint16_t device_id);

// BAR helpers
uint32_t pci_get_bar(pci_device_t *dev, int bar_idx);
int pci_bar_is_io(pci_device_t *dev, int bar_idx);
int pci_bar_is_64bit(pci_device_t *dev, int bar_idx);
uint32_t pci_bar_size(pci_device_t *dev, int bar_idx);

// Debug
void pci_print_device(pci_device_t *dev);
void pci_list_all(void);

#endif