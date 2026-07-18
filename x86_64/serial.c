#include "serial.h"
#include <stdarg.h>

#define PORT 0x3F8

static int serial_initialized = 0;

void serial_init(void) {
    outb(PORT + 1, 0x00);
    outb(PORT + 3, 0x80);
    outb(PORT + 0, 0x03);
    outb(PORT + 1, 0x00);
    outb(PORT + 3, 0x03);
    outb(PORT + 2, 0xC7);
    outb(PORT + 4, 0x0F);
    serial_initialized = 1;
}

void serial_putc(char c) {
    if (!serial_initialized) return;
    while (!(inb(PORT + 5) & 0x20));
    outb(PORT, c);
    if (c == '\n') serial_putc('\r');
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

void kputchar(char c) {
    serial_putc(c);
}

void kputs(const char *s) {
    serial_puts(s);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    int pos = 0;
    for (const char *p = fmt; *p && pos < 511; p++) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case 's': {
                    const char *str = va_arg(ap, const char *);
                    while (*str && pos < 511) buf[pos++] = *str++;
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
                case 'u': {
                    unsigned int val = va_arg(ap, unsigned int);
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
                case 'X': {
                    unsigned int val = va_arg(ap, unsigned int);
                    char tmp[16];
                    int tpos = 0;
                    do {
                        int d = val % 16;
                        tmp[tpos++] = d < 10 ? '0' + d : 'A' + d - 10;
                        val /= 16;
                    } while (val);
                    while (tpos > 0) buf[pos++] = tmp[--tpos];
                    break;
                }
                case 'p': {
                    unsigned long long val = va_arg(ap, unsigned long long);
                    buf[pos++] = '0'; buf[pos++] = 'x';
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
                case 'l': {
                    if (*(p+1) == 'l') { p++; }
                    if (*(p+1) == 'u') {
                        p++;
                        unsigned long long val = va_arg(ap, unsigned long long);
                        char tmp[24];
                        int tpos = 0;
                        do { tmp[tpos++] = '0' + (val % 10); val /= 10; } while (val);
                        while (tpos > 0) buf[pos++] = tmp[--tpos];
                    } else if (*(p+1) == 'x') {
                        p++;
                        unsigned long long val = va_arg(ap, unsigned long long);
                        char tmp[24];
                        int tpos = 0;
                        do {
                            int d = val % 16;
                            tmp[tpos++] = d < 10 ? '0' + d : 'a' + d - 10;
                            val /= 16;
                        } while (val);
                        while (tpos > 0) buf[pos++] = tmp[--tpos];
                    } else if (*(p+1) == 'd') {
                        p++;
                        long long val = va_arg(ap, long long);
                        if (val < 0) { buf[pos++] = '-'; val = -val; }
                        char tmp[24];
                        int tpos = 0;
                        do { tmp[tpos++] = '0' + (val % 10); val /= 10; } while (val);
                        while (tpos > 0) buf[pos++] = tmp[--tpos];
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    buf[pos++] = c;
                    break;
                }
                default:
                    buf[pos++] = '%';
                    if (*p) buf[pos++] = *p;
                    break;
            }
        } else {
            buf[pos++] = *p;
        }
    }
    buf[pos] = 0;
    va_end(ap);
    kputs(buf);
}
