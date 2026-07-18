#include "e1000.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "idt.h"
#include "kernel.h"
#include "serial.h"
#include "heap.h"

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E

#define REG_CTRL      0x0000
#define REG_STATUS    0x0008
#define REG_ICR       0x00C0
#define REG_IMS       0x00D0
#define REG_RCTL      0x0100
#define REG_TCTL      0x0400
#define REG_TIPG      0x0410
#define REG_RDBAL     0x2800
#define REG_RDBAH     0x2804
#define REG_RDLEN     0x2808
#define REG_RDH       0x2810
#define REG_RDT       0x2818
#define REG_TDBAL     0x3800
#define REG_TDBAH     0x3804
#define REG_TDLEN     0x3808
#define REG_TDH       0x3810
#define REG_TDT       0x3818
#define REG_RAL       0x5400
#define REG_RAH       0x5404
#define REG_MTA       0x5200

#define RCTL_EN       0x00000002
#define RCTL_UPE      0x00000008
#define RCTL_MPE      0x00000010
#define RCTL_BAM      0x00008000
#define RCTL_BSIZE_2048 0x00020000
#define RCTL_SECRC    0x04000000

#define TCTL_EN       0x00000002
#define TCTL_PSP      0x00000008
#define TCTL_CT_SHIFT 4
#define TCTL_COLD_SHIFT 12

#define CTRL_RST    0x04000000
#define CTRL_SLU    0x00000040
#define CTRL_FD     0x00000001
#define CTRL_LRST   0x00000008
#define CTRL_ASDE   0x00000020
#define CTRL_FRCSPD 0x00000800
#define CTRL_SPEED  0x00000300

#define REG_MDIC 0x0020
#define MDIC_READ  0x08000000
#define MDIC_WRITE 0x04000000
#define MDIC_READY 0x10000000
#define MDIC_ERROR 0x20000000

#define CMD_EOP 0x01
#define CMD_RS  0x08
#define STAT_DD 0x01

typedef volatile struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) rx_desc_t;

typedef volatile struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) tx_desc_t;

#define NUM_RX 16
#define NUM_TX 8

uint8_t e1000_mac[6];
int e1000_present = 0;
static net_rx_cb_t rx_callback = 0;
static volatile uint32_t *mmio = 0;

static rx_desc_t *rx_descs = 0;
static tx_desc_t *tx_descs = 0;
static uint8_t *rx_bufs[NUM_RX];
static uint8_t tx_buf[2048] __attribute__((aligned(16)));
static volatile int rx_cur = 0;
static volatile int tx_cur = 0;

static inline uint32_t mmio_read(uint16_t reg) { return mmio[reg >> 2]; }
static inline void mmio_write(uint16_t reg, uint32_t val) { mmio[reg >> 2] = val; }

static uint16_t phy_read(uint8_t reg) {
    mmio_write(REG_MDIC, (1 << 21) | (reg << 16) | MDIC_READ);
    for (volatile int i = 0; i < 100000; i++) {
        uint32_t v = mmio_read(REG_MDIC);
        if (v & MDIC_READY) return v & 0xFFFF;
    }
    return 0;
}

static void phy_write(uint8_t reg, uint16_t val) {
    mmio_write(REG_MDIC, (1 << 21) | (reg << 16) | (val & 0xFFFF) | MDIC_WRITE);
    for (volatile int i = 0; i < 100000; i++) {
        if (mmio_read(REG_MDIC) & MDIC_READY) break;
    }
}

void e1000_set_rx_callback(net_rx_cb_t cb) { rx_callback = cb; }

static void e1000_irq_handler(uint64_t frame) {
    (void)frame;
    uint32_t icr = mmio_read(REG_ICR);
    if (icr & 0x04) {
        while (1) {
            int r = rx_cur;
            if (!(rx_descs[r].status & STAT_DD)) break;
            uint16_t len = rx_descs[r].length;
            if (rx_callback) rx_callback(rx_bufs[r], len);
            rx_descs[r].status = 0;
            mmio_write(REG_RDT, r);
            rx_cur = (r + 1) % NUM_RX;
        }
    }
}

int e1000_init(void) {
    pci_device_t dev;
    if (!pci_find_device(E1000_VENDOR, E1000_DEVICE, &dev)) {
        kprintf("[e1000] not found\n");
        return -1;
    }
    kprintf("[e1000] found at %d:%d.%d\n", dev.bus, dev.slot, dev.func);

    uint64_t bar0 = pci_get_bar(dev.bus, dev.slot, dev.func, 0);
    kprintf("[e1000] BAR0 = 0x%lx\n", bar0);

    uint32_t cmd = pci_read_config(dev.bus, dev.slot, dev.func, 0x04);
    cmd |= 0x07;
    pci_write_config(dev.bus, dev.slot, dev.func, 0x04, cmd);

    uint8_t irq_line = pci_read_config(dev.bus, dev.slot, dev.func, 0x3C) & 0xFF;
    kprintf("[e1000] IRQ line = %d\n", irq_line);

    mmio = (volatile uint32_t *)(uintptr_t)bar0;

    // No reset - keep GRUB's initialization

    // Read MAC
    uint32_t ral = mmio_read(REG_RAL);
    uint32_t rah = mmio_read(REG_RAH);
    e1000_mac[0] = ral & 0xFF;
    e1000_mac[1] = (ral >> 8) & 0xFF;
    e1000_mac[2] = (ral >> 16) & 0xFF;
    e1000_mac[3] = (ral >> 24) & 0xFF;
    e1000_mac[4] = rah & 0xFF;
    e1000_mac[5] = (rah >> 8) & 0xFF;
    kprintf("[e1000] MAC %x:%x:%x:%x:%x:%x\n",
        e1000_mac[0], e1000_mac[1], e1000_mac[2],
        e1000_mac[3], e1000_mac[4], e1000_mac[5]);

    // Check EECD auto-read
    uint32_t eecd = mmio_read(0x0010);
    kprintf("[e1000] EECD=0x%x\n", eecd);

    // Write MAC with AV bit, clear MTA
    mmio_write(REG_RAL, ral);
    mmio_write(REG_RAH, rah | (1 << 14));
    for (int i = 0; i < 128; i++) mmio_write(0x5200 + i * 4, 0);

    // Setup RX/TX rings first (before bringing link up)
    uint64_t rx_desc_phys = (uint64_t)pmm_alloc_page();
    rx_descs = (rx_desc_t *)(uintptr_t)rx_desc_phys;
    memset((void *)rx_descs, 0, 4096);

    for (int i = 0; i < NUM_RX; i++) {
        uint64_t buf_phys = (uint64_t)pmm_alloc_page();
        rx_bufs[i] = (uint8_t *)(uintptr_t)buf_phys;
        rx_descs[i].addr = buf_phys;
        rx_descs[i].status = 0;
    }

    uint64_t tx_desc_phys = (uint64_t)pmm_alloc_page();
    tx_descs = (tx_desc_t *)(uintptr_t)tx_desc_phys;
    memset((void *)tx_descs, 0, 4096);

    mmio_write(REG_RDBAL, (uint32_t)(rx_desc_phys & 0xFFFFFFFF));
    mmio_write(REG_RDBAH, (uint32_t)((rx_desc_phys >> 32) & 0xFFFFFFFF));
    mmio_write(REG_RDLEN, NUM_RX * sizeof(rx_desc_t));
    mmio_write(REG_RDH, 0);
    mmio_write(REG_RDT, NUM_RX - 1);

    mmio_write(REG_TDBAL, (uint32_t)(tx_desc_phys & 0xFFFFFFFF));
    mmio_write(REG_TDBAH, (uint32_t)((tx_desc_phys >> 32) & 0xFFFFFFFF));
    mmio_write(REG_TDLEN, NUM_TX * sizeof(tx_desc_t));
    mmio_write(REG_TDH, 0);
    mmio_write(REG_TDT, 0);

    mmio_write(REG_RCTL, RCTL_EN | RCTL_UPE | RCTL_MPE | RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);
    mmio_write(REG_TCTL, TCTL_EN | TCTL_PSP | (0x10 << TCTL_CT_SHIFT) | (0x40 << TCTL_COLD_SHIFT));
    mmio_write(REG_TIPG, 0x0060200A);

    // Now bring link up: clear LRST, set SLU+FD, clear FRCSPD
    uint32_t ctrl = mmio_read(REG_CTRL);
    ctrl |= CTRL_SLU | CTRL_FD;
    ctrl &= ~(CTRL_LRST | CTRL_RST | CTRL_FRCSPD);
    mmio_write(REG_CTRL, ctrl);
    for (volatile int i = 0; i < 500000; i++) asm volatile("pause");

    uint32_t init_status = mmio_read(REG_STATUS);
    kprintf("[e1000] STATUS=0x%x\n", init_status);

    // Debug: read PHY status
    kprintf("[e1000] PHY ID1=0x%x ID2=0x%x\n", phy_read(2), phy_read(3));
    kprintf("[e1000] PHY CTRL=0x%x STATUS=0x%x\n", phy_read(0), phy_read(1));

    // Restart PHY auto-negotiation
    uint16_t phy0 = phy_read(0);
    phy_write(0, phy0 | (1 << 9));
    for (volatile int i = 0; i < 500000; i++) asm volatile("pause");
    kprintf("[e1000] PHY CTRL=0x%x STATUS=0x%x after restart\n", phy_read(0), phy_read(1));

    if (irq_line && irq_line < 16) {
        irq_register_handler(irq_line, e1000_irq_handler);
        mmio_write(REG_IMS, 0x04);
    }

    // Check link status after full init
    uint32_t status = mmio_read(REG_STATUS);
    kprintf("[e1000] STATUS=0x%x link=%s\n", status, (status & 0x02) ? "UP" : "DOWN");

    e1000_present = 1;
    kprintf("[e1000] initialized\n");
    return 0;
}

void e1000_send(const uint8_t *data, uint16_t len) {
    if (!e1000_present) return;
    if (len > 2048) len = 2048;

    memcpy(tx_buf, data, len);
    memset(tx_buf + len, 0, 2048 - len);

    int t = tx_cur;
    tx_descs[t].addr = (uint64_t)(uintptr_t)tx_buf;
    tx_descs[t].length = len;
    tx_descs[t].cmd = CMD_EOP | CMD_RS;
    tx_descs[t].status = 0;
    tx_cur = (t + 1) % NUM_TX;
    asm volatile("mfence" ::: "memory");
    mmio_write(REG_TDT, tx_cur);
    while (!(tx_descs[t].status & STAT_DD)) asm volatile("pause");
}

int e1000_poll(uint8_t *buf, uint16_t *len) {
    if (!e1000_present) return 0;
    int r = rx_cur;
    if (!(rx_descs[r].status & STAT_DD)) return 0;
    *len = rx_descs[r].length;
    if (buf) memcpy(buf, rx_bufs[r], *len);
    rx_descs[r].status = 0;
    mmio_write(REG_RDT, r);
    rx_cur = (r + 1) % NUM_RX;
    return 1;
}

void e1000_poll_all(void) {
    if (!e1000_present || !rx_callback) return;
    uint8_t buf[2048];
    uint16_t len;
    int count = 0;
    while (e1000_poll(buf, &len)) {
        rx_callback(buf, len);
        count++;
    }
    if (count) kprintf("[e1000] rx %d packets\n", count);
}
