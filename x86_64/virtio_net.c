#include "virtio_net.h"
#include "pci.h"
#include "pmm.h"
#include "kernel.h"
#include "serial.h"
#include "heap.h"

#define AMD_VENDOR  0x1022
#define PCNET_DEV   0x2000

// PCnet-PCI II 16-bit register access (NE2100-compatible layout):
// RDP (Register Data Port) at BAR0+0x10
// RAP (Register Address Port) at BAR0+0x12
// BDP (Bus Data Port) at BAR0+0x16
#define RDP_OFF 0x10
#define RAP_OFF 0x12
#define BDP_OFF 0x16

#define CSR0   0
#define CSR1   1
#define CSR2   2
#define CSR3   3
#define CSR4   4
#define CSR5   5
#define CSR12 12
#define CSR13 13
#define CSR14 14
#define CSR15 15
#define CSR58 58
#define CSR76 76
#define CSR78 78

#define RX_RING 16
#define TX_RING 8
#define BUF_SZ  1544

static uint16_t iobase = 0;
uint8_t virtio_mac[6];
int virtio_present = 0;
static net_rx_cb_t rx_cb = 0;

typedef struct {
    uint32_t base;
    uint32_t flags;
    uint32_t status;
    uint32_t reserved;
} __attribute__((packed)) pcnet_desc_t;

static pcnet_desc_t *rx_ring = 0;
static pcnet_desc_t *tx_ring = 0;
static uint32_t rx_ring_phys = 0;
static uint32_t tx_ring_phys = 0;
static uint8_t *rx_bufs[RX_RING];
static uint8_t tx_buf[BUF_SZ] __attribute__((aligned(16)));
static int rx_cur = 0;
static int tx_cur = 0;

static inline void csr_write16(uint16_t rap, uint16_t val) {
    outw(iobase + RAP_OFF, rap);
    io_wait();
    outw(iobase + RDP_OFF, val);
}
static inline uint16_t csr_read16(uint16_t rap) {
    outw(iobase + RAP_OFF, rap);
    io_wait();
    return inw(iobase + RDP_OFF);
}
static inline uint16_t bcr_read(uint16_t bcr) {
    outw(iobase + RAP_OFF, bcr);
    io_wait();
    return inw(iobase + BDP_OFF);
}
static inline void csr_write32(uint16_t low_rap, uint32_t val) {
    csr_write16(low_rap, val & 0xFFFF);
    csr_write16(low_rap + 1, (val >> 16) & 0xFFFF);
}

void virtio_set_rx_callback(net_rx_cb_t cb) { rx_cb = cb; }

int virtio_init(void) {
    pci_device_t dev;
    if (!pci_find_device(AMD_VENDOR, PCNET_DEV, &dev)) {
        kprintf("[pcnet] not found\n");
        return -1;
    }
    kprintf("[pcnet] found at %d:%d.%d\n", dev.bus, dev.slot, dev.func);

    uint64_t bar0 = pci_get_bar(dev.bus, dev.slot, dev.func, 0);
    kprintf("[pcnet] BAR0=0x%lx\n", bar0);

    uint32_t cmd = pci_read_config(dev.bus, dev.slot, dev.func, 4);
    cmd |= 0x07;
    pci_write_config(dev.bus, dev.slot, dev.func, 4, cmd);

    iobase = bar0 & ~1;
    kprintf("[pcnet] IOBASE=0x%x\n", iobase);

    outb(iobase + 0x14, 0);
    for (volatile int i = 0; i < 500000; i++) asm volatile("pause");

    uint16_t csr0_init = csr_read16(CSR0);
    kprintf("[pcnet] CSR0=0x%x after reset\n", csr0_init);

    for (int i = 0; i < 6; i++)
        virtio_mac[i] = inb(iobase + i);
    kprintf("[pcnet] MAC %x:%x:%x:%x:%x:%x\n",
        virtio_mac[0],virtio_mac[1],virtio_mac[2],
        virtio_mac[3],virtio_mac[4],virtio_mac[5]);

    rx_ring_phys = (uint32_t)(uintptr_t)pmm_alloc_page();
    rx_ring = (pcnet_desc_t *)(uintptr_t)rx_ring_phys;
    memset(rx_ring, 0, 4096);

    tx_ring_phys = (uint32_t)(uintptr_t)pmm_alloc_page();
    tx_ring = (pcnet_desc_t *)(uintptr_t)tx_ring_phys;
    memset(tx_ring, 0, 4096);

    for (int i = 0; i < RX_RING; i++) {
        rx_bufs[i] = (uint8_t *)(uintptr_t)pmm_alloc_page();
        rx_ring[i].base = (uint32_t)(uintptr_t)rx_bufs[i];
        rx_ring[i].flags = (0xF << 12) | ((4096 - BUF_SZ) & 0xFFF);
        rx_ring[i].flags |= (1 << 31);
        rx_ring[i].status = 0;
        rx_ring[i].reserved = 0;
    }

    for (int i = 0; i < TX_RING; i++) {
        tx_ring[i].base = 0;
        tx_ring[i].flags = 0;
        tx_ring[i].status = 0;
        tx_ring[i].reserved = 0;
    }

    uint32_t ib_phys = (uint32_t)(uintptr_t)pmm_alloc_page();
    uint16_t *ib = (uint16_t *)(uintptr_t)ib_phys;
    memset(ib, 0, 32);

    // INITBLK32 (SWSTYLE=2), 28 bytes:
    // word 0: mode
    // word 1: res1(3:0) | rlen(7:4) | res2(11:8) | tlen(15:12)
    // word 2: padr1  | word 3: padr2 | word 4: padr3 | word 5: reserved
    // word 6-9: ladrf1-4
    // dword at bytes 20: RDRA | bytes 24: TDRA
    ib[0] = 0x0000;
    ib[1] = (0 << 0) | (4 << 4) | (0 << 8) | (3 << 12);  // rlen=4 (16 entries), tlen=3
    ib[2] = virtio_mac[0] | (virtio_mac[1] << 8);
    ib[3] = virtio_mac[2] | (virtio_mac[3] << 8);
    ib[4] = virtio_mac[4] | (virtio_mac[5] << 8);
    ib[5] = 0;
    ib[6] = ib[7] = ib[8] = ib[9] = 0;
    *(uint32_t *)((uint8_t *)ib + 20) = rx_ring_phys;
    *(uint32_t *)((uint8_t *)ib + 24) = tx_ring_phys;

    kprintf("[pcnet] IB[0]=0x%x IB[1]=0x%x IB[20]=0x%x IB[24]=0x%x\n",
        ib[0], ib[1], *(uint32_t *)((uint8_t *)ib + 20), *(uint32_t *)((uint8_t *)ib + 24));

    csr_write16(CSR5, 0x0000);

    // SWSTYLE=2: 32-bit descriptor mode, INITBLK32
    csr_write16(CSR58, 2);
    kprintf("[pcnet] CSR58=0x%x after SWSTYLE set\n", csr_read16(CSR58));

    csr_write16(CSR3, 0x0000);
    csr_write16(CSR4, 0x0015);
    csr_write16(CSR15, 0x0000);
    csr_write16(CSR76, 4);
    csr_write16(CSR78, 3);

    csr_write16(CSR5, 0x0000);

    csr_write32(CSR1, ib_phys);

    // VirtualBox CSR0 bit layout: INIT=0x0001, STRT=0x0002, TDMD=0x0008,
    // TXON=0x0010, RXON=0x0020, INEA=0x0040, INTR=0x0080, IDON=0x0100

    kprintf("[pcnet] issuing INIT (0x0041)...\n");
    csr_write16(CSR0, 0x0041);
    for (volatile int i = 0; i < 3000000; i++) asm volatile("pause");

    uint16_t csr0 = csr_read16(CSR0);
    kprintf("[pcnet] CSR0=0x%x after INIT\n", csr0);

    uint16_t csr5_val = csr_read16(CSR5);
    kprintf("[pcnet] CSR5=0x%x after INIT\n", csr5_val);

    if (!(csr0 & 0x0100)) {
        kprintf("[pcnet] INIT failed (no IDON)\n");
        return -1;
    }
    kprintf("[pcnet] INIT OK (IDON set)\n");

    if (csr0 & 0x8000)
        kprintf("[pcnet] ERR bit set after INIT\n");

    csr_write16(CSR5, 0x0000);

    kprintf("[pcnet] issuing START (0x0042)...\n");
    csr_write16(CSR0, 0x0042);
    for (volatile int i = 0; i < 3000000; i++) asm volatile("pause");

    csr0 = csr_read16(CSR0);
    kprintf("[pcnet] CSR0=0x%x after START\n", csr0);

    if (csr0 & 0x0010)
        kprintf("[pcnet] TXON enabled\n");
    else
        kprintf("[pcnet] TXON not set\n");

    if (csr0 & 0x0020)
        kprintf("[pcnet] RXON enabled\n");
    else
        kprintf("[pcnet] RXON not set\n");

    uint16_t csr4_start = csr_read16(CSR4);
    kprintf("[pcnet] CSR4=0x%x after START\n", csr4_start);

    uint16_t csr5_start = csr_read16(CSR5);
    kprintf("[pcnet] CSR5=0x%x after START\n", csr5_start);

    csr_write16(CSR5, 0x0000);
    csr5_start = csr_read16(CSR5);
    kprintf("[pcnet] CSR5=0x%x after clear\n", csr5_start);

    kprintf("[pcnet] waiting for link (polling BCR4)...\n");
    uint16_t lnkst;
    int i;
    for (i = 0; i < 100; i++) {
        lnkst = bcr_read(4);
        if (lnkst & 0x40) break;
        for (volatile int j = 0; j < 500000; j++) asm volatile("pause");
    }
    if (lnkst & 0x40)
        kprintf("[pcnet] link UP after %d polls\n", i);
    else
        kprintf("[pcnet] link still down, will retry in virtio_poll_all\n");

    virtio_present = 1;
    kprintf("[pcnet] initialized\n");
    kprintf("[pcnet] CSR12=0x%x CSR13=0x%x CSR14=0x%x (PADR)\n",
        csr_read16(CSR12), csr_read16(CSR13), csr_read16(CSR14));
    return 0;
}

void virtio_send(const uint8_t *data, uint16_t len) {
    if (!virtio_present) return;
    if (len > BUF_SZ) len = BUF_SZ;

    memcpy(tx_buf, data, len);

    int t = tx_cur;
    tx_ring[t].base = (uint32_t)(uintptr_t)tx_buf;
    tx_ring[t].flags = (1 << 31) | (1 << 25) | (1 << 24) | (0xF << 12) | ((4096 - len) & 0xFFF);
    tx_ring[t].status = 0;
    tx_ring[t].reserved = 0;
    // Flush packet data and descriptor to RAM so the VMM sees them.
    for (int i = 0; i < len; i += 64)
        asm volatile("clflush (%0)" :: "r"(tx_buf + i) : "memory");
    asm volatile("clflush (%0)" :: "r"(&tx_ring[t]) : "memory");
    asm volatile("mfence" ::: "memory");

    csr_write16(CSR0, 0x004A); // INEA+TDMD+STRT

    tx_cur = (t + 1) % TX_RING;
}

void virtio_poll_all(void) {
    if (!virtio_present || !rx_cb) return;

    while (1) {
        asm volatile("clflush (%0)" :: "r"(&rx_ring[rx_cur].flags) : "memory");
        asm volatile("mfence" ::: "memory");
        if (rx_ring[rx_cur].flags & (1 << 31))
            break;

        uint32_t flags = rx_ring[rx_cur].flags;
        uint16_t received_len = rx_ring[rx_cur].status & 0x0FFF;

        if (!(flags & (1 << 30)) && received_len > 0) {
            rx_cb(rx_bufs[rx_cur], received_len);
        }

        rx_ring[rx_cur].base = (uint32_t)(uintptr_t)rx_bufs[rx_cur];
        rx_ring[rx_cur].flags = (0xF << 12) | ((4096 - BUF_SZ) & 0xFFF);
        rx_ring[rx_cur].flags |= (1 << 31);
        rx_ring[rx_cur].status = 0;
        rx_ring[rx_cur].reserved = 0;
        asm volatile("clflush (%0)" :: "r"(&rx_ring[rx_cur].flags) : "memory");
        asm volatile("mfence" ::: "memory");
        rx_cur = (rx_cur + 1) % RX_RING;
    }
}
