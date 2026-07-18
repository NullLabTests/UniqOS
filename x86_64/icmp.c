#include "icmp.h"
#include "ip.h"
#include "net.h"
#include "kernel.h"
#include "pit.h"
#include "support.h"

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8

#define PING_TIMEOUT_TICKS 20

static uint16_t icmp_id = 0x1234;
static uint16_t icmp_seq = 0;
static uint64_t ping_start = 0;
static int ping_pending = 0;
static ip_t ping_target;
static ping_cb_t ping_callback;

static uint16_t icmp_checksum(const uint16_t *data, int len) {
    uint32_t sum = 0;
    for (int i = 0; i < len / 2; i++) sum += data[i];
    if (len & 1) sum += ((uint8_t *)data)[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

int ping_send(ip_t target, ping_cb_t callback) {
    if (ping_pending) return -1;
    ping_target = target;
    ping_callback = callback;
    ping_pending = 1;
    ping_start = pit_get_tick();

    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    buf[0] = ICMP_ECHO_REQUEST;
    buf[1] = 0;
    buf[2] = 0; buf[3] = 0;
    buf[4] = (icmp_id >> 8) & 0xFF;
    buf[5] = icmp_id & 0xFF;
    buf[6] = (icmp_seq >> 8) & 0xFF;
    buf[7] = icmp_seq & 0xFF;
    // payload with timestamp
    uint64_t now = pit_get_tick();
    memcpy(buf + 8, &now, 8);

    uint16_t csum = icmp_checksum((uint16_t *)buf, 64);
    buf[2] = (csum >> 8) & 0xFF;
    buf[3] = csum & 0xFF;

    ip_send(target, 1, buf, 64);
    icmp_seq++;
    return 0;
}

void icmp_handle(const uint8_t *data, uint16_t len, ip_t src) {
    if (len < 8) return;
    uint8_t type = data[0];
    uint8_t code = data[1];
    (void)code;

    if (type == ICMP_ECHO_REPLY && ping_pending) {
        uint16_t id = (data[4] << 8) | data[5];
        uint16_t seq = (data[6] << 8) | data[7];
        (void)seq;
        // check source matches target
        if (id == icmp_id &&
            src.addr[0] == ping_target.addr[0] &&
            src.addr[1] == ping_target.addr[1] &&
            src.addr[2] == ping_target.addr[2] &&
            src.addr[3] == ping_target.addr[3]) {
            ping_pending = 0;
            uint64_t *timestamp = (uint64_t *)(data + 8);
            uint64_t now = pit_get_tick();
            uint64_t rtt = now - *timestamp;
            if (ping_callback) ping_callback(src, rtt);
        }
    }

    // Reply to echo requests (type 8) so others can ping us
    if (type == ICMP_ECHO_REQUEST) {
        uint8_t reply[64];
        memset(reply, 0, sizeof(reply));
        reply[0] = ICMP_ECHO_REPLY;
        reply[1] = 0;
        reply[4] = data[4]; reply[5] = data[5];
        reply[6] = data[6]; reply[7] = data[7];
        uint16_t csum = icmp_checksum((uint16_t *)reply, len < 64 ? len : 64);
        reply[2] = (csum >> 8) & 0xFF;
        reply[3] = csum & 0xFF;
        ip_send(src, 1, reply, len < 64 ? len : 64);
    }
}

void icmp_tick(void) {
    if (!ping_pending) return;
    if (pit_get_tick() - ping_start >= PING_TIMEOUT_TICKS) {
        ping_pending = 0;
        if (ping_callback) {
            ip_t empty;
            memset(&empty, 0, sizeof(empty));
            ping_callback(empty, (uint64_t)-1);
        }
    }
}

int ping_busy(void) { return ping_pending; }
