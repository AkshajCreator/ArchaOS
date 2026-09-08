// src/net/e1000.c — Intel PRO/1000 (82540EM) PCI Network Driver
// Cooperative polling model — no interrupts used.
// Descriptor rings are static arrays with __attribute__((aligned(16)))
// so no runtime aligned-allocator dependency.

#include "e1000.h"
#include "../pci.h"
#include "../serial.h"
#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * Minimal memory utilities (no libc in freestanding env)
 * ============================================================ */
static void e_memset(void *dst, uint8_t val, uint32_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = val;
}

static void e_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

/* ============================================================
 * Driver State
 * ============================================================ */
static int      e1000_active = 0;
static uint8_t *e1000_mmio   = 0;      /* BAR0 mapped base  */
static uint8_t  e1000_mac[6] = {0};

/* ---- RX ring ---- */
static e1000_rx_desc_t rx_descs[E1000_NUM_RX_DESC] __attribute__((aligned(16)));
static uint8_t  rx_bufs[E1000_NUM_RX_DESC][E1000_BUF_SIZE] __attribute__((aligned(16)));
static uint32_t rx_tail = 0;

/* ---- TX ring ---- */
static e1000_tx_desc_t tx_descs[E1000_NUM_TX_DESC] __attribute__((aligned(16)));
static uint8_t  tx_bufs[E1000_NUM_TX_DESC][E1000_BUF_SIZE] __attribute__((aligned(16)));
static uint32_t tx_tail = 0;

/* ============================================================
 * MMIO Register Access
 * ============================================================ */
uint32_t e1000_read32(uint32_t reg)
{
    volatile uint32_t *p = (volatile uint32_t *)(e1000_mmio + reg);
    return *p;
}

void e1000_write32(uint32_t reg, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)(e1000_mmio + reg);
    *p = val;
}

/* ============================================================
 * EEPROM Read (auto-read via EERD register)
 * The EEPROM holds the factory MAC address at words 0, 1, 2.
 * ============================================================ */
static uint16_t eeprom_read(uint8_t addr)
{
    /* Write address and START bit */
    e1000_write32(E1000_EERD, ((uint32_t)addr << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START);

    uint32_t eerd;
    /* Spin until DONE bit is set (try both bit-4 and bit-1 variants) */
    for (int i = 0; i < 10000; i++) {
        eerd = e1000_read32(E1000_EERD);
        if (eerd & E1000_EERD_DONE)     goto done;
        if (eerd & E1000_EERD_DONE_ALT) goto done;
        /* tiny spin delay */
        for (volatile int d = 0; d < 100; d++) asm volatile("nop");
    }
done:
    return (uint16_t)(e1000_read32(E1000_EERD) >> E1000_EERD_DATA_SHIFT);
}

/* ============================================================
 * Read MAC from EEPROM words 0-2
 * Each 16-bit word contains 2 bytes of the 6-byte MAC, little-endian.
 * ============================================================ */
static void read_mac_from_eeprom(void)
{
    uint16_t w0 = eeprom_read(0);
    uint16_t w1 = eeprom_read(1);
    uint16_t w2 = eeprom_read(2);

    e1000_mac[0] = (uint8_t)(w0 & 0xFF);
    e1000_mac[1] = (uint8_t)(w0 >> 8);
    e1000_mac[2] = (uint8_t)(w1 & 0xFF);
    e1000_mac[3] = (uint8_t)(w1 >> 8);
    e1000_mac[4] = (uint8_t)(w2 & 0xFF);
    e1000_mac[5] = (uint8_t)(w2 >> 8);
}

/* ============================================================
 * Read MAC from RAL/RAH registers (fallback if EEPROM is zero)
 * ============================================================ */
static void read_mac_from_ral(void)
{
    uint32_t ral = e1000_read32(E1000_RAL);
    uint32_t rah = e1000_read32(E1000_RAH);
    e1000_mac[0] = (uint8_t)(ral & 0xFF);
    e1000_mac[1] = (uint8_t)(ral >> 8);
    e1000_mac[2] = (uint8_t)(ral >> 16);
    e1000_mac[3] = (uint8_t)(ral >> 24);
    e1000_mac[4] = (uint8_t)(rah & 0xFF);
    e1000_mac[5] = (uint8_t)(rah >> 8);
}

/* ============================================================
 * PCI helper — enable Bus Mastering + Memory Space on the NIC
 * ============================================================ */
static void pci_enable_device(pci_device_t *dev)
{
    uint32_t cmd = pci_config_read(dev->bus, dev->device, dev->function, PCI_COMMAND);
    /* Bit 1 = Memory Space Enable, Bit 2 = Bus Master Enable */
    cmd |= (1u << 1) | (1u << 2);
    pci_config_write(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
}

/* ============================================================
 * e1000_init — find the card, map MMIO, set up rings, start NIC
 * ============================================================ */
int e1000_init(void)
{
    /* Try the three common PCI device IDs */
    pci_device_t *dev = pci_find_vendor_device(E1000_VENDOR_ID, E1000_DEV_82540EM);
    if (!dev) dev = pci_find_vendor_device(E1000_VENDOR_ID, E1000_DEV_82545EM);
    if (!dev) dev = pci_find_vendor_device(E1000_VENDOR_ID, E1000_DEV_82574L);
    if (!dev) {
        /* Fall back to class-based lookup (Ethernet = class 0x02, subclass 0x00) */
        dev = pci_find_device(0x02, 0x00, 0xFF);
    }
    if (!dev) {
        serial_puts(COM1_BASE, "[E1000] No Intel NIC found on PCI bus\n");
        return 0;
    }

    /* Enable bus mastering and MMIO decode */
    pci_enable_device(dev);

    /* BAR0 is the 32-bit MMIO base address; mask off the type/flags bits */
    uint32_t bar0 = dev->bar[0];
    if (bar0 & 0x1) {
        serial_puts(COM1_BASE, "[E1000] BAR0 is I/O, not MMIO — unsupported\n");
        return 0;
    }
    e1000_mmio = (uint8_t *)(bar0 & 0xFFFFFFF0);

    serial_printf(COM1_BASE, "[E1000] Found vendor=%04x dev=%04x MMIO=0x%08x\n",
                  dev->vendor_id, dev->device_id, (uint32_t)e1000_mmio);

    /* ---- Software Reset ---- */
    e1000_write32(E1000_CTRL, e1000_read32(E1000_CTRL) | E1000_CTRL_RST);
    /* Short delay for reset to complete */
    for (volatile int i = 0; i < 100000; i++) asm volatile("nop");
    /* Wait until RST bit clears */
    for (int i = 0; i < 10000; i++) {
        if (!(e1000_read32(E1000_CTRL) & E1000_CTRL_RST)) break;
    }

    /* Mask all interrupts — we use polling */
    e1000_write32(E1000_IMC, 0xFFFFFFFF);
    (void)e1000_read32(E1000_ICR); /* Clear any pending */

    /* ---- Set Link Up ---- */
    e1000_write32(E1000_CTRL, e1000_read32(E1000_CTRL) | E1000_CTRL_SLU);

    /* ---- Clear Multicast Table ---- */
    for (int i = 0; i < 128; i++)
        e1000_write32(E1000_MTA + i * 4, 0);

    /* ---- Read MAC Address ---- */
    read_mac_from_eeprom();
    /* If EEPROM returned all zeros, try RAL/RAH */
    if (e1000_mac[0] == 0 && e1000_mac[1] == 0 && e1000_mac[2] == 0)
        read_mac_from_ral();

    serial_printf(COM1_BASE,
        "[E1000] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
        e1000_mac[0], e1000_mac[1], e1000_mac[2],
        e1000_mac[3], e1000_mac[4], e1000_mac[5]);

    /* ---- Set up RAL/RAH with our MAC ---- */
    uint32_t ral_val = (uint32_t)e1000_mac[0]
                     | ((uint32_t)e1000_mac[1] << 8)
                     | ((uint32_t)e1000_mac[2] << 16)
                     | ((uint32_t)e1000_mac[3] << 24);
    uint32_t rah_val = (uint32_t)e1000_mac[4]
                     | ((uint32_t)e1000_mac[5] << 8)
                     | (1u << 31); /* Address Valid bit */
    e1000_write32(E1000_RAL, ral_val);
    e1000_write32(E1000_RAH, rah_val);

    /* ================================================================
     * RX Ring Setup
     * ================================================================ */
    e_memset(rx_descs, 0, sizeof(rx_descs));
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].addr   = (uint64_t)(uint32_t)rx_bufs[i];
        rx_descs[i].status = 0;
    }
    rx_tail = 0;

    /* Physical address of descriptor ring */
    uint32_t rx_phys = (uint32_t)rx_descs;
    e1000_write32(E1000_RDBAL, rx_phys);
    e1000_write32(E1000_RDBAH, 0);
    e1000_write32(E1000_RDLEN, (uint32_t)(E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t)));
    e1000_write32(E1000_RDH, 0);
    e1000_write32(E1000_RDT, E1000_NUM_RX_DESC - 1);

    /* RCTL: enable receiver, broadcast, strip CRC, 2K buffers */
    e1000_write32(E1000_RCTL,
        E1000_RCTL_EN      |
        E1000_RCTL_SBP     |
        E1000_RCTL_UPE     |
        E1000_RCTL_MPE     |
        E1000_RCTL_LBM_NO  |
        E1000_RCTL_RDMTS_HALF |
        E1000_RCTL_BAM     |
        E1000_RCTL_BSIZE_2048 |
        E1000_RCTL_SECRC);

    /* ================================================================
     * TX Ring Setup
     * ================================================================ */
    e_memset(tx_descs, 0, sizeof(tx_descs));
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].addr   = (uint64_t)(uint32_t)tx_bufs[i];
        tx_descs[i].status = E1000_TXD_STAT_DD; /* Mark all as done initially */
    }
    tx_tail = 0;

    uint32_t tx_phys = (uint32_t)tx_descs;
    e1000_write32(E1000_TDBAL, tx_phys);
    e1000_write32(E1000_TDBAH, 0);
    e1000_write32(E1000_TDLEN, (uint32_t)(E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t)));
    e1000_write32(E1000_TDH, 0);
    e1000_write32(E1000_TDT, 0);

    /* TCTL: enable transmitter, pad short packets, standard collision params */
    e1000_write32(E1000_TCTL,
        E1000_TCTL_EN  |
        E1000_TCTL_PSP |
        (15u << E1000_TCTL_CT_SHIFT)   |   /* Collision threshold = 15 */
        (63u << E1000_TCTL_COLD_SHIFT));    /* Collision distance  = 63 */

    /* TIPG: IEEE 802.3 standard inter-packet gap for Gigabit */
    e1000_write32(E1000_TIPG, 0x0060200A);

    e1000_active = 1;
    serial_puts(COM1_BASE, "[E1000] Driver ready\n");
    return 1;
}

/* ============================================================
 * e1000_is_active
 * ============================================================ */
int e1000_is_active(void)
{
    return e1000_active;
}

/* ============================================================
 * e1000_get_mac
 * ============================================================ */
void e1000_get_mac(uint8_t mac[6])
{
    e_memcpy(mac, e1000_mac, 6);
}

/* ============================================================
 * e1000_send — transmit a raw Ethernet frame
 * ============================================================ */
int e1000_send(const void *data, uint16_t len)
{
    if (!e1000_active || !data || len == 0 || len > E1000_BUF_SIZE)
        return 0;

    uint32_t idx = tx_tail % E1000_NUM_TX_DESC;

    /* Wait for this descriptor slot to be free (hardware has consumed it) */
    for (int i = 0; i < 100000; i++) {
        if (tx_descs[idx].status & E1000_TXD_STAT_DD) break;
        asm volatile("nop");
    }
    if (!(tx_descs[idx].status & E1000_TXD_STAT_DD)) {
        serial_puts(COM1_BASE, "[E1000] TX ring full\n");
        return 0;
    }

    /* Copy payload into the static TX buffer */
    e_memcpy(tx_bufs[idx], data, len);

    /* Fill descriptor */
    tx_descs[idx].addr   = (uint64_t)(uint32_t)tx_bufs[idx];
    tx_descs[idx].length = len;
    tx_descs[idx].cso    = 0;
    tx_descs[idx].cmd    = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    tx_descs[idx].status = 0;   /* Clear DD — hardware will set it when done */
    tx_descs[idx].css    = 0;
    tx_descs[idx].special = 0;

    /* Advance tail to trigger transmission */
    tx_tail = (tx_tail + 1) % E1000_NUM_TX_DESC;
    e1000_write32(E1000_TDT, tx_tail);

    return 1;
}

/* ============================================================
 * e1000_recv — poll for a received Ethernet frame
 * Returns the number of bytes copied into buf, 0 if nothing arrived.
 * ============================================================ */
uint16_t e1000_recv(void *buf)
{
    if (!e1000_active || !buf) return 0;

    uint32_t idx = rx_tail % E1000_NUM_RX_DESC;

    /* Check if the hardware has placed a packet here */
    if (!(rx_descs[idx].status & E1000_RXD_STAT_DD)) return 0;

    uint16_t len = rx_descs[idx].length;
    if (len > E1000_BUF_SIZE) len = E1000_BUF_SIZE;

    e_memcpy(buf, rx_bufs[idx], len);

    /* Return descriptor to hardware */
    rx_descs[idx].status = 0;
    e1000_write32(E1000_RDT, idx);

    rx_tail = (rx_tail + 1) % E1000_NUM_RX_DESC;
    return len;
}
