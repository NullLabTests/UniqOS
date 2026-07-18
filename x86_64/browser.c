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

#define BROWSER_BUF 8192

static char browser_content[BROWSER_BUF];
static int browser_len = 0;
static int browser_win = -1;
static int browser_fetching = 0;
static int browser_dns_pending = 0;
static ip_t browser_server_ip;
static char browser_url_host[256];
static char browser_url_path[256];
static char status_text[64] = "";

static void browser_draw(window_t *win);
static void clean_html(const char *src, int srclen, char *dst, int *dstlen);
static void browser_dns_cb(const char *hostname, ip_t addr, int found);
static void browser_http_cb(const char *response, uint16_t len);

static void browser_http_cb(const char *response, uint16_t len) {
    if (len > BROWSER_BUF - 1) len = BROWSER_BUF - 1;
    memcpy(browser_content, response, len);
    browser_content[len] = 0;
    browser_len = len;
    browser_fetching = 0;
    strcpy(status_text, "Done");
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

void browser_init(void) {
    browser_win = window_create(150, 80, 700, 500, "Browser");
    if (browser_win < 0) return;
    window_set_draw(browser_win, browser_draw);
    browser_content[0] = 0;
    browser_len = 0;
    strcpy(status_text, "Init");
    browser_fetch("example.com", "/");
}

void browser_fetch(const char *host, const char *path) {
    if (browser_fetching || browser_dns_pending) return;

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

void browser_tick(void) {
    http_tick();
}

int browser_window_id(void) { return browser_win; }

static void clean_html(const char *src, int srclen, char *dst, int *dstlen) {
    *dstlen = 0;
    int in_script = 0;
    int in_style = 0;
    int skip_nl = 0;

    for (int i = 0; i < srclen && *dstlen < BROWSER_BUF - 4; i++) {
        char c = src[i];

        if (c == '<') {
            if (i + 7 < srclen && src[i+1] == 's' && src[i+2] == 'c' && src[i+3] == 'r' &&
                src[i+4] == 'i' && src[i+5] == 'p' && src[i+6] == 't') {
                in_script = 1;
                while (i < srclen && src[i] != '>') i++;
                continue;
            }
            if (i + 5 < srclen && src[i+1] == 's' && src[i+2] == 't' && src[i+3] == 'y' &&
                src[i+4] == 'l' && src[i+5] == 'e') {
                in_style = 1;
                while (i < srclen && src[i] != '>') i++;
                continue;
            }
            if (in_script || in_style) {
                if (i + 1 < srclen && src[i+1] == '/') {
                    int j = i + 2;
                    while (j < srclen && src[j] != '>') j++;
                    if (j < srclen) {
                        int taglen = j - (i + 2);
                        if (taglen == 6 && memcmp(src + i + 2, "script", 6) == 0) in_script = 0;
                        if (taglen == 5 && memcmp(src + i + 2, "style", 5) == 0) in_style = 0;
                    }
                }
                while (i < srclen && src[i] != '>') i++;
                continue;
            }

            if (i + 3 < srclen && src[i+1] == 'b' && src[i+2] == 'r') {
                dst[*dstlen] = '\n'; (*dstlen)++;
                skip_nl = 0;
                while (i < srclen && src[i] != '>') i++;
                continue;
            }
            if (i + 3 < srclen && ((src[i+1] == 'p' && src[i+2] == '>') ||
                (src[i+1] == 'd' && src[i+2] == 'i' && src[i+3] == 'v'))) {
                if (!skip_nl) { dst[*dstlen] = '\n'; (*dstlen)++; }
                while (i < srclen && src[i] != '>') i++;
                skip_nl = 1;
                continue;
            }
            if (i + 4 < srclen && src[i+1] == '/' && src[i+2] == 'p' && src[i+3] == '>') {
                if (!skip_nl) { dst[*dstlen] = '\n'; (*dstlen)++; }
                i += 3;
                skip_nl = 1;
                continue;
            }
            if (i + 4 < srclen && src[i+1] == 'h' && src[i+2] >= '1' && src[i+2] <= '6' && src[i+3] == '>') {
                dst[*dstlen] = '\n'; (*dstlen)++;
                skip_nl = 0;
                i += 3;
                continue;
            }
            if (i + 5 < srclen && src[i+1] == '/' && src[i+2] == 'h' && src[i+3] >= '1' && src[i+3] <= '6' && src[i+4] == '>') {
                dst[*dstlen] = '\n'; (*dstlen)++;
                i += 4;
                skip_nl = 0;
                continue;
            }
            if (i + 3 < srclen && src[i+1] == 'l' && src[i+2] == 'i' && src[i+3] == '>') {
                dst[*dstlen] = '\n'; (*dstlen)++;
                dst[*dstlen] = ' '; (*dstlen)++;
                dst[*dstlen] = '-'; (*dstlen)++;
                dst[*dstlen] = ' '; (*dstlen)++;
                i += 3;
                skip_nl = 0;
                continue;
            }
            if (i + 4 < srclen && (src[i+1] == 't' && src[i+2] == 'd' && src[i+3] == '>')) {
                dst[*dstlen] = ' '; (*dstlen)++;
                i += 3;
                skip_nl = 0;
                continue;
            }
            if (i + 4 < srclen && src[i+1] == 't' && src[i+2] == 'r' && src[i+3] == '>') {
                dst[*dstlen] = '\n'; (*dstlen)++;
                i += 3;
                skip_nl = 0;
                continue;
            }
            if (i + 4 < srclen && src[i+1] == 't' && src[i+2] == 'h' && src[i+3] == '>') {
                dst[*dstlen] = ' '; (*dstlen)++;
                i += 3;
                skip_nl = 0;
                continue;
            }
            // <a> tags - note for future link handling
            // <b>, <strong>, <em>, <span>, etc. - strip silently

            while (i < srclen && src[i] != '>') i++;
            skip_nl = 0;
            continue;
        }

        if (in_script || in_style) continue;

        if (c == '&') {
            if (i+5 < srclen && src[i+1]=='a' && src[i+2]=='m' && src[i+3]=='p' && src[i+4]==';') {
                dst[*dstlen] = '&'; (*dstlen)++; i += 4; continue;
            }
            if (i+4 < srclen && src[i+1]=='l' && src[i+2]=='t' && src[i+3]==';') {
                i += 3; continue;
            }
            if (i+4 < srclen && src[i+1]=='g' && src[i+2]=='t' && src[i+3]==';') {
                i += 3; continue;
            }
            if (i+6 < srclen && src[i+1]=='q' && src[i+2]=='u' && src[i+3]=='o' && src[i+4]=='t' && src[i+5]==';') {
                dst[*dstlen] = '"'; (*dstlen)++; i += 5; continue;
            }
            if (i+6 < srclen && src[i+1]=='n' && src[i+2]=='b' && src[i+3]=='s' && src[i+4]=='p' && src[i+5]==';') {
                dst[*dstlen] = ' '; (*dstlen)++; i += 5; continue;
            }
            while (i < srclen && src[i] != ';') i++;
            continue;
        }

        if (c == '\n' || c == '\r') {
            dst[*dstlen] = ' '; (*dstlen)++;
            continue;
        }
        if (c == '\t') continue;

        dst[*dstlen] = c; (*dstlen)++;
        skip_nl = 0;
    }
    dst[*dstlen] = 0;
}

static void browser_draw(window_t *win) {
    int wx = win->x, wy = win->y;
    int w = win->w, h = win->h;

    display_fill_rect(wx + 8, wy + win->titlebar_h + 4, w - 16, h - win->titlebar_h - 8, 0x00101010);

    int y = wy + win->titlebar_h + 8;
    int x = wx + 12;

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

    char text[BROWSER_BUF];
    int text_len;
    clean_html(browser_content, browser_len, text, &text_len);

    int max_lines = (h - win->titlebar_h - 16) / 16;
    int max_chars = (w - 24) / 8;
    if (max_chars < 1) max_chars = 1;

    int line = 0;
    int col = 0;

    // Wrap long lines
    for (int i = 0; i < text_len && line < max_lines; i++) {
        char c = text[i];

        if (c == '\n') {
            line++;
            col = 0;
            y += 16;
            continue;
        }

        if (c == ' ' && col == 0) continue;

        display_put_char(x + col * 8, y, c, 0x00CCCCCC, 0x00101010);
        col++;

        if (col >= max_chars) {
            line++;
            col = 0;
            y += 16;
        }
    }

    if (text_len == 0) {
        const char *msg = "(empty)";
        for (int i = 0; msg[i]; i++)
            display_put_char(x + i * 8, y, msg[i], 0x00AA5555, 0x00101010);
    }
}
