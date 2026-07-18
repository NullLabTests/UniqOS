#pragma once

#include "net.h"

typedef void (*http_cb_t)(const char *response, uint16_t len);

void http_get(ip_t server, uint16_t port, const char *host, const char *path, http_cb_t cb);
void http_tick(void);
int http_busy(void);
