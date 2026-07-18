#include "dns.h"
#include "tcp.h"
#include "ip.h"
#include "net.h"
#include "kernel.h"
#include "pit.h"
#include "heap.h"
#include "support.h"

#define DNS_BUF 512

static uint16_t dns_id = 0x1234;

static int dns_conn = -1;
static int dns_pending = 0;
static char dns_host[256];
static dns_cb_t dns_callback;
static ip_t dns_server;
static uint8_t dns_rx_buf[DNS_BUF];
static uint16_t dns_rx_len;
static uint64_t dns_start_tick = 0;

static void dns_connected(void);
static void dns_data(const uint8_t *data, uint16_t len);
static void dns_closed(void);

static void encode_name(uint8_t *buf, uint16_t *pos, const char *name) {
    while (*name) {
        const char *dot = name;
        while (*dot && *dot != '.') dot++;
        uint8_t len = (uint8_t)(dot - name);
        buf[(*pos)++] = len;
        for (const char *p = name; p < dot; p++)
            buf[(*pos)++] = *p;
        name = dot;
        if (*name == '.') name++;
    }
    buf[(*pos)++] = 0;
}

int dns_resolve(const char *hostname, dns_cb_t callback) {
    if (dns_pending) return -1;

    int hlen = 0;
    while (hostname[hlen] && hlen < 255) {
        dns_host[hlen] = hostname[hlen];
        hlen++;
    }
    dns_host[hlen] = 0;
    dns_callback = callback;
    dns_pending = 1;
    dns_rx_len = 0;
    dns_start_tick = pit_get_tick();

    dns_server.addr[0] = 10;
    dns_server.addr[1] = 0;
    dns_server.addr[2] = 2;
    dns_server.addr[3] = 3;

    dns_conn = tcp_connect(dns_server, 53, dns_connected, dns_data, dns_closed);
    if (dns_conn < 0) {
        dns_pending = 0;
        return -1;
    }
    return 1;
}

static void dns_connected(void) {
    uint8_t buf[DNS_BUF];
    uint16_t pos = 0;

    buf[pos++] = (dns_id >> 8) & 0xFF; buf[pos++] = dns_id & 0xFF;
    buf[pos++] = 0x01; buf[pos++] = 0x00;
    buf[pos++] = 0x00; buf[pos++] = 0x01;
    buf[pos++] = 0x00; buf[pos++] = 0x00;
    buf[pos++] = 0x00; buf[pos++] = 0x00;
    buf[pos++] = 0x00; buf[pos++] = 0x00;

    encode_name(buf, &pos, dns_host);

    buf[pos++] = 0x00; buf[pos++] = 0x01;
    buf[pos++] = 0x00; buf[pos++] = 0x01;

    tcp_send(buf, pos);
}

static void dns_data(const uint8_t *data, uint16_t len) {
    if (dns_rx_len + len < DNS_BUF) {
        memcpy(dns_rx_buf + dns_rx_len, data, len);
        dns_rx_len += len;
    }
}

static void dns_closed(void) {
    if (!dns_pending) return;
    dns_pending = 0;

    ip_t result;
    memset(&result, 0, sizeof(result));
    int found = 0;

    if (dns_rx_len >= 12) {
        uint16_t ancount = (dns_rx_buf[6] << 8) | dns_rx_buf[7];
        uint16_t pos = 12;

        while (pos < dns_rx_len && dns_rx_buf[pos] != 0) {
            if (dns_rx_buf[pos] >= 192) { pos += 2; break; }
            else pos += dns_rx_buf[pos] + 1;
        }
        pos++;
        pos += 4;

        for (int i = 0; i < ancount && pos + 12 <= dns_rx_len; i++) {
            if (pos < dns_rx_len && dns_rx_buf[pos] >= 192) pos += 2;
            else { while (pos < dns_rx_len && dns_rx_buf[pos] != 0) pos++; pos++; }

            if (pos + 10 > dns_rx_len) break;
            uint16_t type = (dns_rx_buf[pos] << 8) | dns_rx_buf[pos+1]; pos += 2;
            pos += 2; // class
            pos += 4; // TTL
            if (pos + 2 > dns_rx_len) break;
            uint16_t rdlen = (dns_rx_buf[pos] << 8) | dns_rx_buf[pos+1]; pos += 2;

            if (type == 1 && rdlen == 4 && pos + 4 <= dns_rx_len) {
                result.addr[0] = dns_rx_buf[pos];
                result.addr[1] = dns_rx_buf[pos+1];
                result.addr[2] = dns_rx_buf[pos+2];
                result.addr[3] = dns_rx_buf[pos+3];
                found = 1;
                break;
            }
            pos += rdlen;
        }
    }

    if (dns_callback) {
        dns_callback(dns_host, result, found);
    }
}

void dns_tick(void) {
    if (!dns_pending) return;
    if (pit_get_tick() - dns_start_tick >= 50) {
        kprintf("[dns] timeout for %s\n", dns_host);
        dns_pending = 0;
        dns_conn = -1;
        ip_t empty;
        memset(&empty, 0, sizeof(empty));
        if (dns_callback) dns_callback(dns_host, empty, 0);
    }
}

int dns_busy(void) {
    return dns_pending;
}
