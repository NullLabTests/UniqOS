#pragma once
#include <stdint.h>

typedef void (*net_rx_cb_t)(const uint8_t *data, uint16_t len);

extern uint8_t virtio_mac[6];
extern int virtio_present;

int virtio_init(void);
void virtio_send(const uint8_t *data, uint16_t len);
void virtio_poll_all(void);
void virtio_set_rx_callback(net_rx_cb_t cb);
