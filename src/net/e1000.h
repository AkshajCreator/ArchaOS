// src/net/e1000.h — Intel PRO/1000 (82540EM) PCI Network Driver
// Cooperative polling model: call e1000_recv() periodically, no IRQs needed.
#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * PCI Vendor / Device IDs
 * ============================================================ */
#define E1000_VENDOR_ID     0x8086   /* Intel */
#define E1000_DEV_82540EM   0x100E   /* QEMU default "e1000"           */
#define E1000_DEV_82545EM   0x100F   /* QEMU "e1000-82545em" alternate  */
#define E1000_DEV_82574L    0x10D3   /* Some QEMU/real board variants   */

/* ============================================================
 * MMIO Register Offsets (relative to BAR0 base)
 * ============================================================ */
#define E1000_CTRL      0x0000   /* Device Control                   */
#define E1000_STATUS    0x0008   /* Device Status                    */
#define E1000_EECD      0x0010   /* EEPROM/Flash Control & Data      */
#define E1000_EERD      0x0014   /* EEPROM Read Register             */
#define E1000_ICR       0x00C0   /* Interrupt Cause Read             */
#define E1000_IMS       0x00D0   /* Interrupt Mask Set               */
#define E1000_IMC       0x00D8   /* Interrupt Mask Clear             */
#define E1000_RCTL      0x0100   /* Receive Control                  */
#define E1000_TCTL      0x0400   /* Transmit Control                 */
#define E1000_TIPG      0x0410   /* Transmit IPG Register            */
#define E1000_RDBAL     0x2800   /* RX Descriptor Base Address Low   */
#define E1000_RDBAH     0x2804   /* RX Descriptor Base Address High  */
#define E1000_RDLEN     0x2808   /* RX Descriptor Ring Length        */
#define E1000_RDH       0x2810   /* RX Descriptor Head               */
#define E1000_RDT       0x2818   /* RX Descriptor Tail               */
#define E1000_TDBAL     0x3800   /* TX Descriptor Base Address Low   */
#define E1000_TDBAH     0x3804   /* TX Descriptor Base Address High  */
#define E1000_TDLEN     0x3808   /* TX Descriptor Ring Length        */
#define E1000_TDH       0x3810   /* TX Descriptor Head               */
#define E1000_TDT       0x3818   /* TX Descriptor Tail               */
#define E1000_RAL       0x5400   /* Receive Address Low  (MAC[0..3]) */
#define E1000_RAH       0x5404   /* Receive Address High (MAC[4..5] + valid bit) */
#define E1000_MTA       0x5200   /* Multicast Table Array (128 * 4 bytes) */

/* ============================================================
 * CTRL register bits
 * ============================================================ */
#define E1000_CTRL_SLU      (1u << 6)    /* Set Link Up    */
#define E1000_CTRL_RST      (1u << 26)   /* Device Reset   */

/* ============================================================
 * RCTL register bits
 * ============================================================ */
#define E1000_RCTL_EN         (1u << 1)   /* Receiver Enable        */
#define E1000_RCTL_SBP        (1u << 2)   /* Store Bad Packets      */
#define E1000_RCTL_UPE        (1u << 3)   /* Unicast Promiscuous    */
#define E1000_RCTL_MPE        (1u << 4)   /* Multicast Promiscuous  */
#define E1000_RCTL_LBM_NO     (0u << 6)   /* No Loopback            */
#define E1000_RCTL_RDMTS_HALF (0u << 8)   /* Free buffer threshold  */
#define E1000_RCTL_BAM        (1u << 15)  /* Broadcast Accept Mode  */
#define E1000_RCTL_BSIZE_2048 (0u << 16)  /* Buffer size 2048 bytes */
#define E1000_RCTL_SECRC      (1u << 26)  /* Strip Ethernet CRC     */

/* ============================================================
 * TCTL register bits
 * ============================================================ */
#define E1000_TCTL_EN        (1u << 1)   /* Transmit Enable     */
#define E1000_TCTL_PSP       (1u << 3)   /* Pad Short Packets   */
#define E1000_TCTL_CT_SHIFT  4
#define E1000_TCTL_COLD_SHIFT 12

/* ============================================================
 * TX Descriptor CMD bits
 * ============================================================ */
#define E1000_TXD_CMD_EOP   (1u << 0)   /* End of Packet    */
#define E1000_TXD_CMD_IFCS  (1u << 1)   /* Insert FCS       */
#define E1000_TXD_CMD_RS    (1u << 3)   /* Report Status    */

/* ============================================================
 * RX / TX Descriptor Status bits
 * ============================================================ */
#define E1000_RXD_STAT_DD   (1u << 0)   /* Descriptor Done  */
#define E1000_RXD_STAT_EOP  (1u << 1)   /* End of Packet    */
#define E1000_TXD_STAT_DD   (1u << 0)   /* Descriptor Done  */

/* ============================================================
 * EERD register bits
 * ============================================================ */
#define E1000_EERD_START      (1u << 0)  /* Start Read       */
#define E1000_EERD_DONE       (1u << 4)  /* Done (small EEPROM) */
#define E1000_EERD_DONE_ALT   (1u << 1)  /* Done (alt variant) */
#define E1000_EERD_ADDR_SHIFT 8
#define E1000_EERD_DATA_SHIFT 16

/* ============================================================
 * Descriptor Ring Sizes
 * ============================================================ */
#define E1000_NUM_RX_DESC  32     /* Must be multiple of 8  */
#define E1000_NUM_TX_DESC  8      /* Must be multiple of 8  */
#define E1000_BUF_SIZE     2048   /* Bytes per RX/TX buffer */

/* ============================================================
 * Packed Descriptor Structures (each exactly 16 bytes)
 * ============================================================ */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

/* ============================================================
 * Public API
 * ============================================================ */

/* Initialize E1000. Returns 1 on success, 0 if no card found. */
int      e1000_init(void);

/* Returns 1 if driver initialised successfully. */
int      e1000_is_active(void);

/* Copy MAC address into mac[6]. */
void     e1000_get_mac(uint8_t mac[6]);

/* Transmit a raw Ethernet frame (max 1518 bytes). Returns 1 on success. */
int      e1000_send(const void *data, uint16_t len);

/* Poll for a received frame. Returns bytes copied into buf, 0 if empty.
 * buf must point to at least E1000_BUF_SIZE bytes. */
uint16_t e1000_recv(void *buf);

/* Low-level MMIO register access (exposed for diagnostics). */
uint32_t e1000_read32(uint32_t reg);
void     e1000_write32(uint32_t reg, uint32_t val);

#endif /* E1000_H */
