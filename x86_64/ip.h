#pragma once

#include "net.h"

void ip_init(void);
void ip_send(ip_t dst, uint8_t proto, const uint8_t *data, uint16_t len);
void ip_handle(const uint8_t *data, uint16_t len);
uint16_t ip_checksum(const uint16_t *data, int len);
