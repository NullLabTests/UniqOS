#include "vga.h"
#include <stdarg.h>
#include <stdint.h>

#define VGA_BUF ((volatile uint16_t *)0xB8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t color = 0x07;

void vga_init(void) {
    cursor_x = 0;
    cursor_y = 0;
    color = 0x07;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUF[i] = (uint16_t)color << 8 | ' ';
}

static void scroll(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_BUF[y * VGA_WIDTH + x] = VGA_BUF[(y + 1) * VGA_WIDTH + x];
    for (int x = 0; x < VGA_WIDTH; x++)
        VGA_BUF[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (uint16_t)color << 8 | ' ';
    if (cursor_y > 0) cursor_y--;
}

void vga_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        if (++cursor_y >= VGA_HEIGHT) scroll();
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        return;
    }
    if (c == '\t') {
        int tab = 4 - (cursor_x % 4);
        while (tab--) vga_putchar(' ');
        return;
    }
    VGA_BUF[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)color << 8 | (uint8_t)c;
    if (++cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        if (++cursor_y >= VGA_HEIGHT) scroll();
    }
}

void vga_puts(const char *s) {
    while (*s) vga_putchar(*s++);
}

void vga_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    int pos = 0;
    for (const char *p = fmt; *p && pos < 255; p++) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case 's': {
                    const char *str = va_arg(ap, const char *);
                    while (*str && pos < 255) buf[pos++] = *str++;
                    break;
                }
                case 'd': {
                    int val = va_arg(ap, int);
                    if (val < 0) { buf[pos++] = '-'; val = -val; }
                    char tmp[16];
                    int tpos = 0;
                    do { tmp[tpos++] = '0' + (val % 10); val /= 10; } while (val);
                    while (tpos > 0) buf[pos++] = tmp[--tpos];
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(ap, unsigned int);
                    char tmp[16];
                    int tpos = 0;
                    do {
                        int d = val % 16;
                        tmp[tpos++] = d < 10 ? '0' + d : 'a' + d - 10;
                        val /= 16;
                    } while (val);
                    while (tpos > 0) buf[pos++] = tmp[--tpos];
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(ap, unsigned int);
                    char tmp[16];
                    int tpos = 0;
                    do { tmp[tpos++] = '0' + (val % 10); val /= 10; } while (val);
                    while (tpos > 0) buf[pos++] = tmp[--tpos];
                    break;
                }
                case 'c':
                    buf[pos++] = (char)va_arg(ap, int);
                    break;
                default:
                    buf[pos++] = *p;
                    break;
            }
        } else {
            buf[pos++] = *p;
        }
    }
    buf[pos] = 0;
    va_end(ap);
    vga_puts(buf);
}
