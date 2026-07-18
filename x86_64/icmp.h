#pragma once
#include "net.h"
#include <stdint.h>

typedef void (*ping_cb_t)(ip_t src, uint64_t rtt_ticks);

int ping_send(ip_t target, ping_cb_t callback);
void icmp_handle(const uint8_t *data, uint16_t len, ip_t src);
void icmp_tick(void);
int ping_busy(void);
