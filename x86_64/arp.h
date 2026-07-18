#pragma once

#include "net.h"

void arp_init(void);
int arp_resolve(ip_t ip, mac_t *mac);
void arp_handle(const uint8_t *data, uint16_t len);
void arp_send_request(ip_t ip);
