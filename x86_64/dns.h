#pragma once
#include "net.h"

typedef void (*dns_cb_t)(const char *hostname, ip_t addr, int found);

int dns_resolve(const char *hostname, dns_cb_t callback);
void dns_tick(void);
int dns_busy(void);
