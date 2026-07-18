#include "terminal.h"
#include "font.h"
#include <stdarg.h>

#define FONT_W 8
#define FONT_H 16

static uint32_t *fb = 0;
static int fb_w = 0;
static int fb_h = 0;
static int fb_pitch = 0;
static int cols = 0;
static int rows = 0;
static int cursor_x = 0;
static int cursor_y = 0;
static uint32_t fg_color = 0x00FFFFFF;
static uint32_t bg_color = 0x001A1A2E;
static void (*flush_cb)(void) = 0;

void terminal_init(uint32_t *framebuffer, int width, int height, int pitch, void (*flush)(void)) {
    fb = framebuffer;
    fb_w = width;
    fb_h = height;
    fb_pitch = pitch;
    flush_cb = flush;
    cols = width / FONT_W;
    rows = height / FONT_H;
    cursor_x = 0;
    cursor_y = 0;
    terminal_clear();
}

static void draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if ((unsigned char)c >= 128) return;
    const uint8_t *glyph = font8x16_basic[(unsigned char)c];
    for (int row = 0; row < FONT_H && (y + row) < fb_h; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W && (x + col) < fb_w; col++) {
            fb[(y + row) * (fb_pitch / 4) + (x + col)] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

static void clear_row(int row) {
    int y = row * FONT_H;
    for (int r = 0; r < FONT_H; r++) {
        for (int c = 0; c < fb_w; c++) {
            fb[(y + r) * (fb_pitch / 4) + c] = bg_color;
        }
    }
}

void terminal_clear(void) {
    for (int r = 0; r < rows; r++) clear_row(r);
    cursor_x = 0; cursor_y = 0;
    if (flush_cb) {
        for (volatile int i = 0; i < 1000000; i++);
        flush_cb();
    }
}

void terminal_scroll(void) {
    for (int row = 0; row < rows - 1; row++) {
        int src_y = (row + 1) * FONT_H;
        int dst_y = row * FONT_H;
        for (int r = 0; r < FONT_H; r++) {
            for (int c = 0; c < fb_w; c++) {
                fb[(dst_y + r) * (fb_pitch / 4) + c] = fb[(src_y + r) * (fb_pitch / 4) + c];
            }
        }
    }
    clear_row(rows - 1);
    cursor_y--;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= rows) terminal_scroll();
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        return;
    }
    if (c == '\t') {
        int tab = 4 - (cursor_x % 4);
        while (tab--) terminal_putchar(' ');
        return;
    }
    if (c == '\b') {
        if (cursor_x > 0) cursor_x--;
        return;
    }

    draw_char(cursor_x * FONT_W, cursor_y * FONT_H, c, fg_color, bg_color);
    cursor_x++;
    if (flush_cb) flush_cb();
    if (cursor_x >= cols) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= rows) terminal_scroll();
    }
}

void terminal_write(const char *s) {
    while (*s) terminal_putchar(*s++);
}

void terminal_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    int pos = 0;
    for (const char *p = fmt; *p && pos < 511; p++) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case 's': {
                    const char *s = va_arg(ap, const char *);
                    while (*s && pos < 511) buf[pos++] = *s++;
                    break;
                }
                case 'd': {
                    int val = va_arg(ap, int);
                    if (val < 0) { buf[pos++] = '-'; val = -val; }
                    char tmp[16]; int tpos = 0;
                    do { tmp[tpos++] = '0' + (val % 10); val /= 10; } while (val);
                    while (tpos > 0) buf[pos++] = tmp[--tpos];
                    break;
                }
                case 'u': {
                    unsigned val = va_arg(ap, unsigned);
                    char tmp[16]; int tpos = 0;
                    do { tmp[tpos++] = '0' + (val % 10); val /= 10; } while (val);
                    while (tpos > 0) buf[pos++] = tmp[--tpos];
                    break;
                }
                case 'x': {
                    unsigned val = va_arg(ap, unsigned);
                    char tmp[16]; int tpos = 0;
                    do { int d = val % 16; tmp[tpos++] = d < 10 ? '0'+d : 'a'+d-10; val /= 16; } while (val);
                    while (tpos > 0) buf[pos++] = tmp[--tpos];
                    break;
                }
                case 'p': {
                    uint64_t val = va_arg(ap, uint64_t);
                    buf[pos++] = '0'; buf[pos++] = 'x';
                    char tmp[16]; int tpos = 0;
                    do { int d = val % 16; tmp[tpos++] = d < 10 ? '0'+d : 'a'+d-10; val /= 16; } while (val);
                    while (tpos > 0) buf[pos++] = tmp[--tpos];
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    buf[pos++] = c;
                    break;
                }
                case 'l': {
                    if (*(p+1) == 'l') p++;
                    if (*(p+1) == 'u') { p++;
                        unsigned long long val = va_arg(ap, unsigned long long);
                        char tmp[24]; int tpos = 0;
                        do { tmp[tpos++] = '0' + (val % 10); val /= 10; } while (val);
                        while (tpos > 0) buf[pos++] = tmp[--tpos];
                    } else if (*(p+1) == 'x') { p++;
                        unsigned long long val = va_arg(ap, unsigned long long);
                        char tmp[24]; int tpos = 0;
                        do { int d = val % 16; tmp[tpos++] = d < 10 ? '0'+d : 'a'+d-10; val /= 16; } while (val);
                        while (tpos > 0) buf[pos++] = tmp[--tpos];
                    }
                    break;
                }
                default: buf[pos++] = *p; break;
            }
        } else {
            buf[pos++] = *p;
        }
    }
    buf[pos] = 0;
    va_end(ap);
    terminal_write(buf);
}

void terminal_set_fg(uint32_t color) { fg_color = color; }
void terminal_set_bg(uint32_t color) { bg_color = color; }
void terminal_set_cursor(int row, int col) { cursor_y = row; cursor_x = col; }
