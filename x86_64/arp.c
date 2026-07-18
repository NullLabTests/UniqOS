#include "arp.h"
#include "virtio_net.h"
#include "kernel.h"
#include "heap.h"
#include "serial.h"

#define ARP_CACHE_SIZE 8

static struct { ip_t ip; mac_t mac; int valid; } arp_cache[ARP_CACHE_SIZE];

static void mac_copy(void *dst, const void *src) {
    for (int i = 0; i < 6; i++) ((uint8_t *)dst)[i] = ((const uint8_t *)src)[i];
}

static int mac_eq(const void *a, const void *b) {
    for (int i = 0; i < 6; i++)
        if (((const uint8_t *)a)[i] != ((const uint8_t *)b)[i]) return 0;
    return 1;
}

static int ip_eq(const void *a, const void *b) {
    for (int i = 0; i < 4; i++)
        if (((const uint8_t *)a)[i] != ((const uint8_t *)b)[i]) return 0;
    return 1;
}

void arp_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
}

static int arp_dbg;
void arp_handle(const uint8_t *data, uint16_t len) {
    if (len < sizeof(eth_hdr_t) + sizeof(arp_pkt_t)) return;
    eth_hdr_t *eth = (eth_hdr_t *)data;
    arp_pkt_t *arp = (arp_pkt_t *)(data + sizeof(eth_hdr_t));

    if (arp_dbg++ < 5)
        kprintf("[arp] rx htype=0x%x ptype=0x%x hlen=%d plen=%d oper=0x%x\n",
            arp->htype, arp->ptype, arp->hlen, arp->plen, arp->oper);

    if (arp->htype != 0x0100 || arp->ptype != 0x0008 || arp->hlen != 6 || arp->plen != 4)
        return;

    int is_request = arp->oper == 0x0100;
    int is_reply = arp->oper == 0x0200;

    if (is_reply) {
        kprintf("[arp] reply from %d.%d.%d.%d MAC %x:%x:%x:%x:%x:%x\n",
            arp->spa.addr[0], arp->spa.addr[1], arp->spa.addr[2], arp->spa.addr[3],
            arp->sha.addr[0], arp->sha.addr[1], arp->sha.addr[2],
            arp->sha.addr[3], arp->sha.addr[4], arp->sha.addr[5]);
        int found = 0;
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (arp_cache[i].valid && ip_eq(&arp_cache[i].ip, &arp->spa)) {
                mac_copy(&arp_cache[i].mac, &arp->sha);
                found = 1;
                break;
            }
        }
        if (!found) {
            for (int i = 0; i < ARP_CACHE_SIZE; i++) {
                if (!arp_cache[i].valid) {
                    arp_cache[i].valid = 1;
                    arp_cache[i].ip = arp->spa;
                    mac_copy(&arp_cache[i].mac, &arp->sha);
                    break;
                }
            }
        }
    }

    if (is_request && ip_eq(&arp->tpa, &net_ip)) {
        uint8_t reply[sizeof(eth_hdr_t) + sizeof(arp_pkt_t)];
        eth_hdr_t *re = (eth_hdr_t *)reply;
        arp_pkt_t *ra = (arp_pkt_t *)(reply + sizeof(eth_hdr_t));

        mac_copy(&re->dst, &eth->src);
        mac_copy(&re->src, &net_mac);
        re->type = 0x0608;

        ra->htype = 0x0100;
        ra->ptype = 0x0008;
        ra->hlen = 6;
        ra->plen = 4;
        ra->oper = 0x0200;
        mac_copy(&ra->sha, &net_mac);
        mac_copy(&ra->spa, &arp->tpa);
        mac_copy(&ra->tha, &arp->sha);
        mac_copy(&ra->tpa, &arp->spa);

        virtio_send(reply, sizeof(reply));
    }
}

void arp_send_request(ip_t ip) {
    uint8_t pkt[sizeof(eth_hdr_t) + sizeof(arp_pkt_t)];
    eth_hdr_t *eth = (eth_hdr_t *)pkt;
    arp_pkt_t *arp = (arp_pkt_t *)(pkt + sizeof(eth_hdr_t));

    memset(&eth->dst, 0xFF, 6);
    mac_copy(&eth->src, &net_mac);
    eth->type = 0x0608;

    arp->htype = 0x0100;
    arp->ptype = 0x0008;
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = 0x0100;
    mac_copy(&arp->sha, &net_mac);
    mac_copy(&arp->spa, &net_ip);
    memset(&arp->tha, 0, 6);
    mac_copy(&arp->tpa, &ip);

    kprintf("[arp] request for %d.%d.%d.%d\n",
        ip.addr[0], ip.addr[1], ip.addr[2], ip.addr[3]);
    virtio_send(pkt, sizeof(pkt));
}

int arp_resolve(ip_t ip, mac_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && ip_eq(&arp_cache[i].ip, &ip)) {
            *mac = arp_cache[i].mac;
            return 1;
        }
    }
    return 0;
}
