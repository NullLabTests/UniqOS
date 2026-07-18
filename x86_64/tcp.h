#pragma once

#include "net.h"

typedef void (*tcp_connected_cb_t)(void);
typedef void (*tcp_data_cb_t)(const uint8_t *data, uint16_t len);
typedef void (*tcp_closed_cb_t)(void);

int tcp_connect(ip_t dst, uint16_t port,
                tcp_connected_cb_t on_connected,
                tcp_data_cb_t on_data,
                tcp_closed_cb_t on_closed);
void tcp_send(const uint8_t *data, uint16_t len);
void tcp_close(void);
void tcp_handle(const uint8_t *data, uint16_t len, ip_t src, ip_t dst);
void tcp_tick(void);
