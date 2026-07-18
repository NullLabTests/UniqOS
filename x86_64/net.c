#include "net.h"
#include "virtio_net.h"
#include "arp.h"
#include "ip.h"
#include "serial.h"

mac_t net_mac;
ip_t net_ip;
ip_t net_gw;
ip_t net_dns;
ip_t net_mask;

void net_init(void) {
    net_mac = *(mac_t *)virtio_mac;
    net_ip.addr[0] = 10;
    net_ip.addr[1] = 0;
    net_ip.addr[2] = 2;
    net_ip.addr[3] = 15;
    net_gw.addr[0] = 10;
    net_gw.addr[1] = 0;
    net_gw.addr[2] = 2;
    net_gw.addr[3] = 1;
    net_dns.addr[0] = 10;
    net_dns.addr[1] = 0;
    net_dns.addr[2] = 2;
    net_dns.addr[3] = 3;
    net_mask.addr[0] = 255;
    net_mask.addr[1] = 255;
    net_mask.addr[2] = 255;
    net_mask.addr[3] = 0;

    arp_init();
    ip_init();
}

static int rx_dbg;
void net_rx_dispatch(const uint8_t *data, uint16_t len) {
    if (len < sizeof(eth_hdr_t)) return;
    eth_hdr_t *eth = (eth_hdr_t *)data;

    if (rx_dbg++ < 10)
        kprintf("[net] RX %s len=%d\n",
            eth->type == 0x0608 ? "ARP" : eth->type == 0x0008 ? "IP" : "?",
            len);

    if (eth->type == 0x0608) {
        arp_handle(data, len);
    } else if (eth->type == 0x0008) {
        ip_handle(data, len);
    }
}
