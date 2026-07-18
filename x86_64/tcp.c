#include "tcp.h"
#include "ip.h"
#include "pit.h"
#include "kernel.h"
#include "heap.h"

#define TCP_BUF_SIZE 4096

enum tcp_state {
    TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED,
    TCP_FIN_WAIT1, TCP_FIN_WAIT2, TCP_TIME_WAIT
};

static enum tcp_state tcp_state = TCP_CLOSED;
static ip_t tcp_remote;
static uint16_t tcp_remote_port;
static uint16_t tcp_local_port;

static uint32_t tcp_snd_seq;
static uint32_t tcp_rcv_seq;
static uint32_t tcp_rcv_ack;

static tcp_connected_cb_t tcp_on_connected;
static tcp_data_cb_t tcp_on_data;
static tcp_closed_cb_t tcp_on_closed;

static uint16_t tcp_rx_len = 0;
static uint8_t tcp_tx_buf[TCP_BUF_SIZE];

static uint64_t tcp_retry_tick = 0;
static int tcp_retry_count = 0;

static uint16_t tcp_checksum(const ip_t *src, const ip_t *dst,
                              uint8_t proto, const uint16_t *data, int len) {
    struct {
        ip_t src;
        ip_t dst;
        uint8_t zero;
        uint8_t proto;
        uint16_t len;
    } __attribute__((packed)) pseudo;
    pseudo.src = *src;
    pseudo.dst = *dst;
    pseudo.zero = 0;
    pseudo.proto = proto;
    pseudo.len = ((len >> 8) & 0xFF) | ((len & 0xFF) << 8);

    uint32_t sum = 0;
    uint16_t *p = (uint16_t *)&pseudo;
    for (int i = 0; i < 6; i++) sum += p[i];
    p = (uint16_t *)data;
    for (int i = 0; i < len / 2; i++) sum += p[i];
    if (len & 1) sum += ((uint8_t *)data)[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

static void tcp_send_segment(uint8_t flags, uint32_t seq, uint32_t ack,
                              const uint8_t *payload, uint16_t paylen) {
    uint16_t hdrlen = sizeof(tcp_hdr_t);
    uint8_t seg[hdrlen + paylen];
    tcp_hdr_t *tcp = (tcp_hdr_t *)seg;

    tcp->src_port = ((tcp_local_port >> 8) & 0xFF) | ((tcp_local_port & 0xFF) << 8);
    tcp->dst_port = ((tcp_remote_port >> 8) & 0xFF) | ((tcp_remote_port & 0xFF) << 8);
    tcp->seq = ((seq >> 24) & 0xFF) | ((seq >> 8) & 0xFF00) | ((seq & 0xFF00) << 8) | ((seq & 0xFF) << 24);
    tcp->ack = ((ack >> 24) & 0xFF) | ((ack >> 8) & 0xFF00) | ((ack & 0xFF00) << 8) | ((ack & 0xFF) << 24);
    tcp->offset_res = (hdrlen / 4) << 4;
    tcp->flags = flags;
    tcp->window = 0x2000;
    tcp->window = ((tcp->window >> 8) & 0xFF) | ((tcp->window & 0xFF) << 8);
    tcp->urgent = 0;
    tcp->checksum = 0;

    if (paylen > 0) memcpy(seg + hdrlen, payload, paylen);

    tcp->checksum = tcp_checksum(&net_ip, &tcp_remote, IP_PROTO_TCP,
                                  (uint16_t *)seg, hdrlen + paylen);

    ip_send(tcp_remote, IP_PROTO_TCP, seg, hdrlen + paylen);
}

int tcp_connect(ip_t dst, uint16_t port,
                tcp_connected_cb_t on_connected,
                tcp_data_cb_t on_data,
                tcp_closed_cb_t on_closed) {
    if (tcp_state != TCP_CLOSED) return -1;

    tcp_remote = dst;
    tcp_remote_port = port;
    tcp_local_port = 0x3412;
    tcp_snd_seq = 0x12345678;
    tcp_rcv_seq = 0;
    tcp_rcv_ack = 0;
    tcp_on_connected = on_connected;
    tcp_on_data = on_data;
    tcp_on_closed = on_closed;
    tcp_rx_len = 0;

    tcp_state = TCP_SYN_SENT;
    tcp_retry_count = 0;
    tcp_retry_tick = pit_get_tick();

    tcp_send_segment(TCP_SYN, tcp_snd_seq, 0, 0, 0);
    tcp_snd_seq++;
    return 0;
}

void tcp_send(const uint8_t *data, uint16_t len) {
    if (tcp_state != TCP_ESTABLISHED) return;
    if (len > TCP_BUF_SIZE) len = TCP_BUF_SIZE;
    memcpy(tcp_tx_buf, data, len);
    tcp_send_segment(TCP_PSH | TCP_ACK, tcp_snd_seq, tcp_rcv_seq, tcp_tx_buf, len);
    tcp_snd_seq += len;
}

void tcp_close(void) {
    if (tcp_state == TCP_ESTABLISHED) {
        tcp_state = TCP_FIN_WAIT1;
        tcp_send_segment(TCP_FIN | TCP_ACK, tcp_snd_seq, tcp_rcv_seq, 0, 0);
        tcp_snd_seq++;
    }
}

void tcp_handle(const uint8_t *data, uint16_t len, ip_t src, ip_t dst) {
    (void)dst;
    if (len < sizeof(tcp_hdr_t)) return;

    tcp_hdr_t *tcp = (tcp_hdr_t *)data;
    uint16_t sport = ((tcp->src_port >> 8) & 0xFF) | ((tcp->src_port & 0xFF) << 8);
    uint16_t dport = ((tcp->dst_port >> 8) & 0xFF) | ((tcp->dst_port & 0xFF) << 8);
    uint32_t seq = ((tcp->seq >> 24) | ((tcp->seq >> 8) & 0xFF00) |
                    ((tcp->seq & 0xFF00) << 8) | ((tcp->seq & 0xFF) << 24));
    uint32_t ack = ((tcp->ack >> 24) | ((tcp->ack >> 8) & 0xFF00) |
                    ((tcp->ack & 0xFF00) << 8) | ((tcp->ack & 0xFF) << 24));
    uint8_t flags = tcp->flags;
    uint8_t offset = (tcp->offset_res >> 4) * 4;

    if (dport != tcp_local_port || sport != tcp_remote_port) return;

    uint16_t paylen = (len > offset) ? len - offset : 0;
    const uint8_t *payload = (paylen > 0) ? (data + offset) : 0;

    if (flags & TCP_RST) {
        tcp_state = TCP_CLOSED;
        if (tcp_on_closed) tcp_on_closed();
        return;
    }

    if (tcp_state == TCP_SYN_SENT && (flags & TCP_SYN) && (flags & TCP_ACK)) {
        tcp_rcv_seq = seq + 1;
        tcp_rcv_ack = ack;
        tcp_state = TCP_ESTABLISHED;
        tcp_send_segment(TCP_ACK, tcp_snd_seq, tcp_rcv_seq, 0, 0);
        if (tcp_on_connected) tcp_on_connected();
        return;
    }

    if (tcp_state == TCP_ESTABLISHED) {
        if (paylen > 0 && flags & TCP_ACK) {
            tcp_rcv_seq = seq + paylen;
            tcp_send_segment(TCP_ACK, tcp_snd_seq, tcp_rcv_seq, 0, 0);
            if (tcp_on_data) tcp_on_data(payload, paylen);
        }
        if (flags & TCP_FIN) {
            tcp_rcv_seq = seq + 1;
            tcp_send_segment(TCP_ACK | TCP_FIN, tcp_snd_seq, tcp_rcv_seq, 0, 0);
            tcp_snd_seq++;
            tcp_state = TCP_CLOSED;
            if (tcp_on_closed) tcp_on_closed();
            return;
        }
    }
}

void tcp_tick(void) {
    if (tcp_state == TCP_SYN_SENT && tcp_retry_count < 5) {
        uint64_t now = pit_get_tick();
        if (now - tcp_retry_tick >= 10) {
            tcp_retry_tick = now;
            tcp_retry_count++;
            tcp_send_segment(TCP_SYN, tcp_snd_seq - 1, 0, 0, 0);
        }
    }
}
