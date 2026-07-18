#include "tcp.h"
#include "ip.h"
#include "pit.h"
#include "kernel.h"
#include "heap.h"

#define TCP_BUF_SIZE 4096
#define TCP_MAX_RETRY 10

enum tcp_state {
    TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED,
    TCP_FIN_WAIT1, TCP_FIN_WAIT2, TCP_TIME_WAIT
};

// multiple connections support
#define TCP_MAX_CONN 4
static struct {
    int used;
    enum tcp_state state;
    ip_t remote;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t snd_seq, rcv_seq, snd_una;
    uint8_t tx_buf[TCP_BUF_SIZE];
    uint16_t tx_len;
    uint64_t retry_tick;
    int retry_count;
    int retry_data;
    tcp_connected_cb_t on_connected;
    tcp_data_cb_t on_data;
    tcp_closed_cb_t on_closed;
    void *user_data;
} tcp_conns[TCP_MAX_CONN];

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

static void conn_send_segment(int ci, uint8_t flags, uint32_t seq, uint32_t ack,
                               const uint8_t *payload, uint16_t paylen) {
    if (ci < 0 || ci >= TCP_MAX_CONN || !tcp_conns[ci].used) return;
    uint16_t hdrlen = sizeof(tcp_hdr_t);
    uint8_t seg[hdrlen + paylen];
    tcp_hdr_t *tcp = (tcp_hdr_t *)seg;

    tcp->src_port = ((tcp_conns[ci].local_port >> 8) & 0xFF) | ((tcp_conns[ci].local_port & 0xFF) << 8);
    tcp->dst_port = ((tcp_conns[ci].remote_port >> 8) & 0xFF) | ((tcp_conns[ci].remote_port & 0xFF) << 8);
    tcp->seq = ((seq >> 24) & 0xFF) | ((seq >> 8) & 0xFF00) | ((seq & 0xFF00) << 8) | ((seq & 0xFF) << 24);
    tcp->ack = ((ack >> 24) & 0xFF) | ((ack >> 8) & 0xFF00) | ((ack & 0xFF00) << 8) | ((ack & 0xFF) << 24);
    tcp->offset_res = (hdrlen / 4) << 4;
    tcp->flags = flags;
    tcp->window = 0x2000;
    tcp->window = ((tcp->window >> 8) & 0xFF) | ((tcp->window & 0xFF) << 8);
    tcp->urgent = 0;
    tcp->checksum = 0;

    if (paylen > 0) memcpy(seg + hdrlen, payload, paylen);

    tcp->checksum = tcp_checksum(&net_ip, &tcp_conns[ci].remote, IP_PROTO_TCP,
                                  (uint16_t *)seg, hdrlen + paylen);

    ip_send(tcp_conns[ci].remote, IP_PROTO_TCP, seg, hdrlen + paylen);
}

int tcp_connect(ip_t dst, uint16_t port,
                tcp_connected_cb_t on_connected,
                tcp_data_cb_t on_data,
                tcp_closed_cb_t on_closed) {
    int ci = -1;
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        if (!tcp_conns[i].used) { ci = i; break; }
    }
    if (ci < 0) return -1;

    tcp_conns[ci].used = 1;
    tcp_conns[ci].remote = dst;
    tcp_conns[ci].remote_port = port;
    tcp_conns[ci].local_port = 0x3412 + ci;
    tcp_conns[ci].snd_seq = 0x12345678 + (ci * 0x1000);
    tcp_conns[ci].rcv_seq = 0;
    tcp_conns[ci].snd_una = tcp_conns[ci].snd_seq;
    tcp_conns[ci].on_connected = on_connected;
    tcp_conns[ci].on_data = on_data;
    tcp_conns[ci].on_closed = on_closed;
    tcp_conns[ci].tx_len = 0;
    tcp_conns[ci].state = TCP_SYN_SENT;
    tcp_conns[ci].retry_count = 0;
    tcp_conns[ci].retry_tick = pit_get_tick();
    tcp_conns[ci].retry_data = 0;

    conn_send_segment(ci, TCP_SYN, tcp_conns[ci].snd_seq, 0, 0, 0);
    tcp_conns[ci].snd_seq++;
    return ci;
}

static int conn_by_port(uint16_t dport, uint16_t sport) {
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        if (tcp_conns[i].used &&
            tcp_conns[i].local_port == dport &&
            tcp_conns[i].remote_port == sport) return i;
    }
    return -1;
}

void tcp_send(int ci, const uint8_t *data, uint16_t len) {
    if (ci < 0 || ci >= TCP_MAX_CONN || !tcp_conns[ci].used) return;
    if (tcp_conns[ci].state != TCP_ESTABLISHED) return;
    if (len > TCP_BUF_SIZE) len = TCP_BUF_SIZE;
    memcpy(tcp_conns[ci].tx_buf, data, len);
    tcp_conns[ci].tx_len = len;
    conn_send_segment(ci, TCP_PSH | TCP_ACK, tcp_conns[ci].snd_seq, tcp_conns[ci].rcv_seq,
                       tcp_conns[ci].tx_buf, len);
    tcp_conns[ci].snd_una = tcp_conns[ci].snd_seq;
    tcp_conns[ci].snd_seq += len;
    tcp_conns[ci].retry_data = 1;
    tcp_conns[ci].retry_count = 0;
    tcp_conns[ci].retry_tick = pit_get_tick();
}

void tcp_close(int ci) {
    if (ci < 0 || ci >= TCP_MAX_CONN || !tcp_conns[ci].used) return;
    if (tcp_conns[ci].state == TCP_ESTABLISHED) {
        tcp_conns[ci].state = TCP_FIN_WAIT1;
        conn_send_segment(ci, TCP_FIN | TCP_ACK, tcp_conns[ci].snd_seq, tcp_conns[ci].rcv_seq, 0, 0);
        tcp_conns[ci].snd_seq++;
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

    int ci = conn_by_port(dport, sport);
    if (ci < 0) return;

    uint16_t paylen = (len > offset) ? len - offset : 0;
    const uint8_t *payload = (paylen > 0) ? (data + offset) : 0;

    if (flags & TCP_RST) {
        tcp_conns[ci].state = TCP_CLOSED;
        tcp_conns[ci].used = 0;
        if (tcp_conns[ci].on_closed) tcp_conns[ci].on_closed();
        return;
    }

    if (tcp_conns[ci].state == TCP_SYN_SENT && (flags & TCP_SYN) && (flags & TCP_ACK)) {
        tcp_conns[ci].rcv_seq = seq + 1;
        tcp_conns[ci].state = TCP_ESTABLISHED;
        conn_send_segment(ci, TCP_ACK, tcp_conns[ci].snd_seq, tcp_conns[ci].rcv_seq, 0, 0);
        if (tcp_conns[ci].on_connected) tcp_conns[ci].on_connected();
        return;
    }

    if (tcp_conns[ci].state == TCP_ESTABLISHED) {
        // data acknowledgment
        if (flags & TCP_ACK && tcp_conns[ci].retry_data) {
            if ((int32_t)(ack - tcp_conns[ci].snd_una) > 0) {
                tcp_conns[ci].retry_data = 0;
            }
        }
        if (paylen > 0 && flags & TCP_ACK) {
            tcp_conns[ci].rcv_seq = seq + paylen;
            conn_send_segment(ci, TCP_ACK, tcp_conns[ci].snd_seq, tcp_conns[ci].rcv_seq, 0, 0);
            if (tcp_conns[ci].on_data) tcp_conns[ci].on_data(payload, paylen);
        }
        if (flags & TCP_FIN) {
            tcp_conns[ci].rcv_seq = seq + 1;
            conn_send_segment(ci, TCP_ACK | TCP_FIN, tcp_conns[ci].snd_seq, tcp_conns[ci].rcv_seq, 0, 0);
            tcp_conns[ci].snd_seq++;
            tcp_conns[ci].state = TCP_CLOSED;
            tcp_conns[ci].used = 0;
            if (tcp_conns[ci].on_closed) tcp_conns[ci].on_closed();
            return;
        }
    }

    if (tcp_conns[ci].state == TCP_FIN_WAIT1) {
        if (flags & TCP_ACK) {
            tcp_conns[ci].state = TCP_FIN_WAIT2;
        }
        if (flags & TCP_FIN) {
            tcp_conns[ci].state = TCP_CLOSED;
            tcp_conns[ci].used = 0;
            if (tcp_conns[ci].on_closed) tcp_conns[ci].on_closed();
        }
    }

    if (tcp_conns[ci].state == TCP_FIN_WAIT2) {
        if (flags & TCP_FIN) {
            conn_send_segment(ci, TCP_ACK, tcp_conns[ci].snd_seq, seq + 1, 0, 0);
            tcp_conns[ci].state = TCP_CLOSED;
            tcp_conns[ci].used = 0;
            if (tcp_conns[ci].on_closed) tcp_conns[ci].on_closed();
        }
    }
}

void tcp_tick(void) {
    for (int ci = 0; ci < TCP_MAX_CONN; ci++) {
        if (!tcp_conns[ci].used) continue;
        uint64_t now = pit_get_tick();

        // SYN retransmit
        if (tcp_conns[ci].state == TCP_SYN_SENT && tcp_conns[ci].retry_count < 5) {
            uint64_t delay = 10 << tcp_conns[ci].retry_count;
            if (now - tcp_conns[ci].retry_tick >= delay) {
                tcp_conns[ci].retry_tick = now;
                tcp_conns[ci].retry_count++;
                conn_send_segment(ci, TCP_SYN, tcp_conns[ci].snd_seq - 1, 0, 0, 0);
            }
        }

        // DATA retransmit with exponential backoff
        if (tcp_conns[ci].state == TCP_ESTABLISHED && tcp_conns[ci].retry_data) {
            uint64_t delay = 20 << tcp_conns[ci].retry_count;
            if (delay > 200) delay = 200;
            if (now - tcp_conns[ci].retry_tick >= delay) {
                tcp_conns[ci].retry_tick = now;
                tcp_conns[ci].retry_count++;
                conn_send_segment(ci, TCP_PSH | TCP_ACK,
                                   tcp_conns[ci].snd_una,
                                   tcp_conns[ci].rcv_seq,
                                   tcp_conns[ci].tx_buf,
                                   tcp_conns[ci].tx_len);
            }
        }
    }
}
