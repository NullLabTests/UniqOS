#pragma once

#include <stdint.h>

int e1000_init(void);
void e1000_send(const uint8_t *data, uint16_t len);
int e1000_poll(uint8_t *buf, uint16_t *len);
typedef void (*net_rx_cb_t)(const uint8_t *data, uint16_t len);
void e1000_set_rx_callback(net_rx_cb_t cb);
void e1000_poll_all(void);
extern uint8_t e1000_mac[6];
extern int e1000_present;
