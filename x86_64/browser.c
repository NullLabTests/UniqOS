#include "browser.h"
#include "http.h"
#include "tcp.h"
#include "ip.h"
#include "net.h"
#include "window.h"
#include "display.h"
#include "heap.h"
#include "kernel.h"
#include "desktop.h"

#define BROWSER_BUF 4096

static char browser_content[BROWSER_BUF];
static int browser_len = 0;
static int browser_win = -1;
static int browser_fetching = 0;

static void browser_draw(window_t *win);

static void browser_http_cb(const char *response, uint16_t len) {
    if (len > BROWSER_BUF - 1) len = BROWSER_BUF - 1;
    memcpy(browser_content, response, len);
    browser_content[len] = 0;
    browser_len = len;
    browser_fetching = 0;
}

void browser_init(void) {
    browser_win = window_create(150, 80, 700, 500, "Browser");
    if (browser_win < 0) return;
    window_set_draw(browser_win, browser_draw);
    browser_content[0] = 0;
    browser_len = 0;
    browser_fetch("example.com", "/");
}

void browser_fetch(const char *host, const char *path) {
    if (browser_fetching) return;
    browser_fetching = 1;

    ip_t server;
    server.addr[0] = 93;
    server.addr[1] = 184;
    server.addr[2] = 216;
    server.addr[3] = 34;

    http_get(server, 80, host, path, browser_http_cb);
}

void browser_tick(void) {
    http_tick();
}

int browser_window_id(void) {
    return browser_win;
}

void browser_draw(window_t *win) {
    (void)win;
    int wx = win->x, wy = win->y;

    display_fill_rect(wx + 8, wy + 32, win->w - 16, win->h - 40, 0x00101010);

    int y = wy + 36;
    int x = wx + 12;

    if (browser_fetching) {
        const char *msg = "Fetching...";
        for (int i = 0; msg[i]; i++) {
            display_put_char(x + i * 8, y, msg[i], 0x00888888, 0x00101010);
        }
        return;
    }

    if (browser_len == 0) {
        const char *msg = "Click to fetch example.com";
        for (int i = 0; msg[i]; i++) {
            display_put_char(x + i * 8, y, msg[i], 0x00888888, 0x00101010);
        }
        return;
    }

    int max_chars = (win->w - 24) / 8;
    int max_lines = (win->h - 44) / 16;
    int line = 0;
    int col = 0;

    for (int i = 0; i < browser_len && line < max_lines; i++) {
        char c = browser_content[i];
        if (c == '\n') {
            line++;
            col = 0;
        } else if (c == '\r') {
        } else if (c == '<') {
            while (i < browser_len && browser_content[i] != '>') i++;
        } else if (c == '&') {
            // skip html entities for now
        } else {
            display_put_char(x + col * 8, y + line * 16, c, 0x00CCCCCC, 0x00101010);
            col++;
            if (col >= max_chars) {
                line++;
                col = 0;
            }
        }
    }
}
