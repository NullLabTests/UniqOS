#include "http.h"
#include "tcp.h"
#include "ip.h"
#include "kernel.h"
#include "heap.h"

#define HTTP_BUF 4096

enum http_state {
    HTTP_IDLE, HTTP_CONNECTING, HTTP_SENDING, HTTP_RECEIVING, HTTP_DONE
};

static enum http_state http_st = HTTP_IDLE;
static char http_buf[HTTP_BUF];
static uint16_t http_len = 0;
static http_cb_t http_callback = 0;
static char http_host[256];
static char http_path[256];

static void http_tcp_connected(void) {
    http_st = HTTP_SENDING;
}

static void http_tcp_data(const uint8_t *data, uint16_t len) {
    http_st = HTTP_RECEIVING;
    if (http_len + len < HTTP_BUF) {
        memcpy(http_buf + http_len, data, len);
        http_len += len;
        http_buf[http_len] = 0;
    }
}

static void http_tcp_closed(void) {
    http_st = HTTP_DONE;
    if (http_callback) http_callback(http_buf, http_len);
}

void http_get(ip_t server, uint16_t port, const char *host, const char *path, http_cb_t cb) {
    if (http_st != HTTP_IDLE) return;
    http_st = HTTP_CONNECTING;
    http_len = 0;
    http_callback = cb;
    if (host) {
        int i = 0;
        while (host[i] && i < 255) { http_host[i] = host[i]; i++; }
        http_host[i] = 0;
    } else { http_host[0] = 0; }
    if (path) {
        int i = 0;
        while (path[i] && i < 255) { http_path[i] = path[i]; i++; }
        http_path[i] = 0;
    } else { http_path[0] = '/'; http_path[1] = 0; }

    tcp_connect(server, port, http_tcp_connected, http_tcp_data, http_tcp_closed);
}

void http_tick(void) {
    if (http_st == HTTP_SENDING) {
        char req[1024];
        int pos = 0;
        const char *path = http_path[0] ? http_path : "/";
        const char *host = http_host[0] ? http_host : "example.com";

        const char *get_line = "GET ";
        while (*get_line && pos < 1022) req[pos++] = *get_line++;
        while (*path && pos < 1022) req[pos++] = *path++;
        const char *http_line = " HTTP/1.0\r\n";
        while (*http_line && pos < 1022) req[pos++] = *http_line++;
        const char *host_line = "Host: ";
        while (*host_line && pos < 1022) req[pos++] = *host_line++;
        while (*host && pos < 1022) req[pos++] = *host++;
        const char *crlf = "\r\n";
        while (*crlf && pos < 1022) req[pos++] = *crlf++;
        const char *conn = "Connection: close\r\n\r\n";
        while (*conn && pos < 1022) req[pos++] = *conn++;
        req[pos] = 0;

        tcp_send((uint8_t *)req, pos);
        http_st = HTTP_RECEIVING;
    }
    tcp_tick();
}

int http_busy(void) {
    return http_st != HTTP_IDLE && http_st != HTTP_DONE;
}
