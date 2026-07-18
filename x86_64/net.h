#pragma once

#include <stdint.h>

#define ETH_ALEN 6
#define IP_ALEN 4

typedef struct { uint8_t addr[ETH_ALEN]; } __attribute__((packed)) mac_t;
typedef struct { uint8_t addr[IP_ALEN]; } __attribute__((packed)) ip_t;

typedef struct {
    mac_t dst;
    mac_t src;
    uint16_t type;
} __attribute__((packed)) eth_hdr_t;

#define ETH_TYPE_IP  0x0800
#define ETH_TYPE_ARP 0x0806

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    mac_t sha;
    ip_t spa;
    mac_t tha;
    ip_t tpa;
} __attribute__((packed)) arp_pkt_t;

#define ARP_REQUEST 1
#define ARP_REPLY   2

typedef struct {
    uint8_t ver_ihl;
    uint8_t dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    ip_t src;
    ip_t dst;
} __attribute__((packed)) ip_hdr_t;

#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t offset_res;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_hdr_t;

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed)) udp_hdr_t;

extern mac_t net_mac;
extern ip_t net_ip;
extern ip_t net_gw;
extern ip_t net_dns;
extern ip_t net_mask;

void net_init(void);
void net_rx_dispatch(const uint8_t *data, uint16_t len);
