#include "browser.h"
#include "http.h"
#include "dns.h"
#include "tcp.h"
#include "ip.h"
#include "net.h"
#include "window.h"
#include "display.h"
#include "font.h"
#include "heap.h"
#include "kernel.h"
#include "desktop.h"
#include "support.h"
#include "html_parse.h"
#include "css_minimal.h"
#include "layout.h"
#include "render.h"
#include "link_map.h"

#define BROWSER_BUF 16384
#define HISTORY_DEPTH 16

static char browser_content[BROWSER_BUF];
static int browser_len = 0;
static int browser_win = -1;
static int browser_fetching = 0;
static int browser_dns_pending = 0;
static ip_t browser_server_ip;
static char browser_url_host[256];
static char browser_url_path[256];
static char status_text[64] = "";

static layout_box_t *browser_boxes = 0;
static int browser_box_count = 0;
static link_rect_t *browser_links = 0;
static int browser_link_count = 0;

static char hist_host[HISTORY_DEPTH][256];
static char hist_path[HISTORY_DEPTH][256];
static int hist_pos = -1;
static int hist_count = 0;

static void browser_draw(window_t *win);
static void browser_dns_cb(const char *hostname, ip_t addr, int found);
static void browser_http_cb(const char *response, uint16_t len);
static void browser_parse_content(void);
static void push_history(const char *host, const char *path);
static int browser_handle_click(window_t *win, int mx, int my);

void browser_init(void) {
    browser_win = window_create(150, 80, 700, 500, "Browser");
    if (browser_win < 0) return;
    window_set_draw(browser_win, browser_draw);
    window_set_onclick(browser_win, browser_handle_click);
    browser_content[0] = 0;
    browser_len = 0;
    strcpy(status_text, "Init");
    browser_fetch("example.com", "/");
}

void browser_fetch(const char *host, const char *path) {
    if (!host) return;
    if (browser_fetching || browser_dns_pending) return;

    if (browser_boxes) { layout_free_boxes(browser_boxes, browser_box_count); browser_boxes = 0; browser_box_count = 0; }
    if (browser_links) { link_map_free(browser_links, browser_link_count); browser_links = 0; browser_link_count = 0; }

    strcpy(browser_url_host, host);
    strcpy(browser_url_path, path);
    browser_content[0] = 0;
    browser_len = 0;
    browser_fetching = 1;
    strcpy(status_text, "DNS lookup...");

    if (dns_resolve(host, browser_dns_cb) == 0) {
        browser_dns_pending = 1;
    } else {
        strcpy(status_text, "DNS error");
        browser_fetching = 0;
    }
}

void browser_go_back(void) {
    if (hist_count == 0) return;
    int target = hist_pos - 1;
    if (target < 0) return;
    int old_pos = hist_pos;
    browser_fetch(hist_host[target], hist_path[target]);
    hist_pos = target;
    hist_count = old_pos;
}

static void push_history(const char *host, const char *path) {
    if (hist_pos >= 0 && strcmp(hist_host[hist_pos], host) == 0 && strcmp(hist_path[hist_pos], path) == 0)
        return;
    hist_pos++;
    if (hist_pos >= HISTORY_DEPTH) {
        for (int i = 1; i < HISTORY_DEPTH; i++) {
            strcpy(hist_host[i-1], hist_host[i]);
            strcpy(hist_path[i-1], hist_path[i]);
        }
        hist_pos = HISTORY_DEPTH - 1;
    }
    strcpy(hist_host[hist_pos], host);
    strcpy(hist_path[hist_pos], path);
    hist_count = hist_pos + 1;
}

static void browser_http_cb(const char *response, uint16_t len) {
    if (len > BROWSER_BUF - 1) len = BROWSER_BUF - 1;
    memcpy(browser_content, response, len);
    browser_content[len] = 0;
    browser_len = len;
    browser_fetching = 0;
    push_history(browser_url_host, browser_url_path);
    browser_parse_content();
    kprintf("[browser] HTTP done, %d bytes\n", len);
}

static void browser_dns_cb(const char *hostname, ip_t addr, int found) {
    (void)hostname;
    browser_dns_pending = 0;
    if (found) {
        browser_server_ip = addr;
        kprintf("[browser] DNS: %s = %d.%d.%d.%d\n", hostname,
            addr.addr[0], addr.addr[1], addr.addr[2], addr.addr[3]);
        http_get(addr, 80, browser_url_host, browser_url_path, browser_http_cb);
    } else {
        kprintf("[browser] DNS failed, using fallback IP\n");
        strcpy(status_text, "DNS fallback");
        ip_t fallback;
        fallback.addr[0] = 104; fallback.addr[1] = 20;
        fallback.addr[2] = 23; fallback.addr[3] = 154;
        browser_server_ip = fallback;
        http_get(fallback, 80, browser_url_host, browser_url_path, browser_http_cb);
    }
}

static void browser_parse_content(void) {
    if (browser_len <= 0) return;

    html_node_t *dom = html_parse(browser_content, browser_len);
    if (!dom) {
        strcpy(status_text, "Parse error");
        return;
    }

    window_t *win = window_get(browser_win);
    int viewport_w = win ? (win->w - 24) : 680;
    if (viewport_w < 100) viewport_w = 100;

    if (browser_boxes) { layout_free_boxes(browser_boxes, browser_box_count); browser_boxes = 0; }
    if (browser_links) { link_map_free(browser_links, browser_link_count); browser_links = 0; }

    layout_document(dom, viewport_w, &browser_boxes, &browser_box_count);

    int wx = win ? (win->x + 8) : 158;
    int wy = win ? (win->y + win->titlebar_h + 4) : 108;
    link_map_build(browser_boxes, browser_box_count, wx, wy, &browser_links, &browser_link_count);

    html_free(dom);
    strcpy(status_text, "Loaded");
    kprintf("[browser] %d layout boxes, %d links\n", browser_box_count, browser_link_count);
}

void browser_tick(void) {
    http_tick();
}

int browser_window_id(void) { return browser_win; }

int browser_handle_click(window_t *win, int mx, int my) {
    (void)win;
    if (browser_fetching || browser_dns_pending) return 0;
    if (!browser_links || browser_link_count <= 0) return 0;

    char *url = 0;
    if (link_map_click(browser_links, browser_link_count, mx, my, &url)) {
        if (url) {
            char host[256], path[256];
            strcpy(host, url);
            char *slash = host;
            if (strncmp(host, "http://", 7) == 0) slash = host + 7;
            else if (strncmp(host, "https://", 8) == 0) slash = host + 8;
            char *ps = slash;
            while (*ps && *ps != '/') ps++;
            if (*ps == '/') { *ps = 0; strcpy(path, ps + 1); }
            else { path[0] = '/'; path[1] = 0; }
            strcpy(host, slash);
            if (host[0] == 0) {
                strcpy(host, browser_url_host);
                strcpy(path, url);
                if (path[0] != '/') {
                    char tmp[256];
                    strcpy(tmp, browser_url_path);
                    char *last = tmp + strlen(tmp);
                    while (last > tmp && *last != '/') last--;
                    if (*last == '/') last[1] = 0;
                    strcat(tmp, url);
                    strcpy(path, tmp);
                }
            }
            if (strcmp(host, browser_url_host) == 0 || host[0] != 0) {
                browser_fetch(host, path);
                return 1;
            }
        }
    }
    return 0;
}

static void browser_draw(window_t *win) {
    int wx = win->x, wy = win->y;
    int w = win->w, h = win->h;
    int y = wy + win->titlebar_h + 8;
    int x = wx + 12;

    display_fill_rect(wx + 8, wy + win->titlebar_h + 4, w - 16, h - win->titlebar_h - 8, 0x00101010);

    if (browser_fetching || browser_dns_pending) {
        const char *msg = status_text[0] ? status_text : "Loading...";
        for (int i = 0; msg[i]; i++)
            display_put_char(x + i * 8, y, msg[i], 0x00888888, 0x00101010);
        return;
    }

    if (browser_len == 0) {
        const char *msg = "No content loaded.";
        for (int i = 0; msg[i]; i++)
            display_put_char(x + i * 8, y, msg[i], 0x00888888, 0x00101010);
        return;
    }

    if (browser_boxes && browser_box_count > 0) {
        int cw = w - 16, ch = h - win->titlebar_h - 8;
        int cx = wx + 8, cy = wy + win->titlebar_h + 4;
        render_boxes(browser_boxes, browser_box_count, cx, cy, cw, ch);
    } else {
        const char *msg = status_text[0] ? status_text : "(empty)";
        for (int i = 0; msg[i]; i++)
            display_put_char(x + i * 8, y, msg[i], 0x00AA5555, 0x00101010);
    }
}
