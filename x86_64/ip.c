#include "ip.h"
#include "arp.h"
#include "virtio_net.h"
#include "kernel.h"
#include "heap.h"

extern void tcp_handle(const uint8_t *data, uint16_t len, ip_t src, ip_t dst);
extern void icmp_handle(const uint8_t *data, uint16_t len, ip_t src);

static uint16_t ip_id = 0;

uint16_t ip_checksum(const uint16_t *data, int len) {
    uint32_t sum = 0;
    for (int i = 0; i < len / 2; i++) sum += data[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

void ip_init(void) {}

void ip_send(ip_t dst, uint8_t proto, const uint8_t *data, uint16_t len) {
    if (sizeof(ip_hdr_t) + len > 1500) return;
    uint16_t total = sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + len;
    uint8_t buf[total];
    eth_hdr_t *eth = (eth_hdr_t *)buf;
    ip_hdr_t *ip = (ip_hdr_t *)(buf + sizeof(eth_hdr_t));
    uint8_t *payload = buf + sizeof(eth_hdr_t) + sizeof(ip_hdr_t);

    memcpy(payload, data, len);

    ip->ver_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_len = (sizeof(ip_hdr_t) + len);
    ip->total_len = ((ip->total_len >> 8) & 0xFF) | ((ip->total_len & 0xFF) << 8);
    ip->id = ip_id++;
    ip->id = ((ip->id >> 8) & 0xFF) | ((ip->id & 0xFF) << 8);
    ip->flags_frag = 0x4000;
    ip->flags_frag = ((ip->flags_frag >> 8) & 0xFF) | ((ip->flags_frag & 0xFF) << 8);
    ip->ttl = 64;
    ip->protocol = proto;
    ip->src = net_ip;
    ip->dst = dst;
    ip->checksum = 0;
    ip->checksum = ip_checksum((uint16_t *)ip, sizeof(ip_hdr_t));

    ip_t arp_target;
    int is_local = 1;
    for (int i = 0; i < 4; i++) {
        if ((net_ip.addr[i] & net_mask.addr[i]) != (dst.addr[i] & net_mask.addr[i]))
            is_local = 0;
    }
    if (is_local) {
        arp_target = dst;
    } else {
        arp_target = net_gw;
    }

    mac_t dst_mac;
    if (!arp_resolve(arp_target, &dst_mac)) {
        arp_send_request(arp_target);
        return;
    }

    for (int i = 0; i < 6; i++) eth->dst.addr[i] = dst_mac.addr[i];
    for (int i = 0; i < 6; i++) eth->src.addr[i] = net_mac.addr[i];
    eth->type = 0x0008;

    virtio_send(buf, total);
}

void ip_handle(const uint8_t *data, uint16_t len) {
    if (len < sizeof(eth_hdr_t) + sizeof(ip_hdr_t)) return;
    eth_hdr_t *eth = (eth_hdr_t *)data;
    if (eth->type != 0x0008) return;

    ip_hdr_t *ip = (ip_hdr_t *)(data + sizeof(eth_hdr_t));
    int ihl = (ip->ver_ihl & 0x0F) * 4;
    uint16_t total = ((ip->total_len >> 8) & 0xFF) | ((ip->total_len & 0xFF) << 8);
    uint8_t proto = ip->protocol;

    if (proto == IP_PROTO_TCP && total >= ihl) {
        tcp_handle(data + sizeof(eth_hdr_t) + ihl, total - ihl, ip->src, ip->dst);
    } else if (proto == 1 && total >= ihl) {
        icmp_handle(data + sizeof(eth_hdr_t) + ihl, total - ihl, ip->src);
    }
}
