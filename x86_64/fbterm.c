#include "fbterm.h"
#include "font.h"
#include <stdarg.h>

#define FONT_W 8
#define FONT_H 16

static uint32_t *fb = 0;
static int fb_w = 1024;
static int fb_h = 768;
static int fg = 0x00FFFFFF;
static int bg = 0x00222244;

typedef struct {
    char c;
    uint32_t fg, bg;
} cell_t;

static cell_t cells[128][80];
static int cols = 80, rows = 48;
static int cx = 0, cy = 0;
static int dirty = 1;

void fbterm_init(uint32_t *framebuffer, int w, int h) {
    fb = framebuffer; fb_w = w; fb_h = h;
    cols = w / FONT_W; if (cols > 80) cols = 80;
    rows = h / FONT_H; if (rows > 48) rows = 48;
    cx = 0; cy = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            cells[r][c].c = ' ', cells[r][c].fg = fg, cells[r][c].bg = bg;
    dirty = 1;
}

void fbterm_putchar(char c) {
    if (c == '\n') { cx = 0; cy++; if (cy >= rows) fbterm_scroll(); dirty = 1; return; }
    if (c == '\r') { cx = 0; dirty = 1; return; }
    if (c == '\t') { int n = 4 - (cx % 4); while (n--) fbterm_putchar(' '); return; }
    if (c == '\b') { if (cx > 0) cx--; dirty = 1; return; }
    if ((unsigned char)c < 32) return;
    if (cx >= cols) { cx = 0; cy++; if (cy >= rows) fbterm_scroll(); }
    cells[cy][cx].c = c;
    cells[cy][cx].fg = fg;
    cells[cy][cx].bg = bg;
    cx++; dirty = 1;
}

void fbterm_write(const char *s) { while (*s) fbterm_putchar(*s++); }

void fbterm_printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[512]; int pos = 0;
    for (const char *p = fmt; *p && pos < 511; p++) {
        if (*p == '%') { p++;
            if (*p == 's') { const char *s = va_arg(ap, const char *); while (*s && pos < 511) buf[pos++] = *s++; }
            else if (*p == 'd') { int v = va_arg(ap, int); if (v < 0) { buf[pos++] = '-'; v = -v; } char t[16]; int tp = 0; do { t[tp++] = '0' + (v % 10); v /= 10; } while (v); while (tp > 0) buf[pos++] = t[--tp]; }
            else if (*p == 'u') { unsigned v = va_arg(ap, unsigned); char t[16]; int tp = 0; do { t[tp++] = '0' + (v % 10); v /= 10; } while (v); while (tp > 0) buf[pos++] = t[--tp]; }
            else if (*p == 'x') { unsigned v = va_arg(ap, unsigned); char t[16]; int tp = 0; do { int d = v % 16; t[tp++] = d < 10 ? '0'+d : 'a'+d-10; v /= 16; } while (v); while (tp > 0) buf[pos++] = t[--tp]; }
            else if (*p == 'p') { uint64_t v = va_arg(ap, uint64_t); buf[pos++] = '0'; buf[pos++] = 'x'; char t[16]; int tp = 0; do { int d = v % 16; t[tp++] = d < 10 ? '0'+d : 'a'+d-10; v /= 16; } while (v); while (tp > 0) buf[pos++] = t[--tp]; }
            else if (*p == 'c') { char c = (char)va_arg(ap, int); buf[pos++] = c; }
            else if (*p == 'l') { if (*(p+1) == 'l') p++; if (*(p+1) == 'u') { p++; unsigned long long v = va_arg(ap, unsigned long long); char t[24]; int tp = 0; do { t[tp++] = '0' + (v % 10); v /= 10; } while (v); while (tp > 0) buf[pos++] = t[--tp]; } else if (*(p+1) == 'x') { p++; unsigned long long v = va_arg(ap, unsigned long long); char t[24]; int tp = 0; do { int d = v % 16; t[tp++] = d < 10 ? '0'+d : 'a'+d-10; v /= 16; } while (v); while (tp > 0) buf[pos++] = t[--tp]; } }
            else buf[pos++] = *p;
        } else buf[pos++] = *p;
    }
    buf[pos] = 0; va_end(ap);
    fbterm_write(buf);
}

void fbterm_clear(void) {
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            cells[r][c].c = ' ', cells[r][c].fg = fg, cells[r][c].bg = bg;
    cx = 0; cy = 0; dirty = 1;
}

void fbterm_scroll(void) {
    for (int r = 0; r < rows - 1; r++)
        for (int c = 0; c < cols; c++)
            cells[r][c] = cells[r + 1][c];
    for (int c = 0; c < cols; c++)
        cells[rows - 1][c].c = ' ', cells[rows - 1][c].fg = fg, cells[rows - 1][c].bg = bg;
    cy = rows - 1; dirty = 1;
}

void fbterm_render(void) {
    if (!dirty) return;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            cell_t *cell = &cells[r][c];
            const uint8_t *glyph = font8x16_basic[(unsigned char)cell->c];
            int bx = c * FONT_W, by = r * FONT_H;
            for (int row = 0; row < FONT_H && (by + row) < fb_h; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < FONT_W && (bx + col) < fb_w; col++)
                    fb[(by + row) * fb_w + (bx + col)] = (bits & (0x80 >> col)) ? cell->fg : cell->bg;
            }
        }
    }
    dirty = 0;
}

void fbterm_set_fg(uint32_t c) { fg = c; }
void fbterm_set_bg(uint32_t c) { bg = c; }
