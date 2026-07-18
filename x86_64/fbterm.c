#include "fbterm.h"
#include "font.h"
#include <stdarg.h>

#define FONT_W 8
#define FONT_H 16

static uint32_t *fb = 0;
static int fb_w = 1024;  // framebuffer width (stride)
static int fb_h = 768;   // framebuffer height
static int fb_ox = 0;    // x offset for rendering
static int fb_oy = 0;    // y offset for rendering
static int term_w = 680; // terminal area width
static int term_h = 400; // terminal area height
static int fg = 0x00FFFFFF;
static int bg = 0x00222244;

// ANSI color palette (dark + bright)
static const uint32_t ansi_colors[16] = {
    0x00000000, 0x00CC0000, 0x0000CC00, 0x00CCCC00, // black,red,green,yellow
    0x000000CC, 0x00CC00CC, 0x0000CCCC, 0x00CCCCCC, // blue,magenta,cyan,white
    0x00666666, 0x00FF4444, 0x0044FF44, 0x00FFFF44, // bright black,red,green,yellow
    0x004444FF, 0x00FF44FF, 0x0044FFFF, 0x00FFFFFF, // bright blue,magenta,cyan,white
};

typedef struct {
    char c;
    uint32_t fg, bg;
} cell_t;

static cell_t cells[128][80];
static int cols = 80, rows = 48;
static int cx = 0, cy = 0;
static int dirty = 1;

static int ansi_parse(const char **s) {
    // Parse \033[N[m style sequences
    const char *p = *s;
    if (*p != '\033') return 0;
    p++;
    if (*p != '[') return 0;
    p++;

    while (*p) {
        int val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }

        if (*p == ';') { p++; continue; }
        if (*p == 'm') {
            *s = p + 1;
            if (val == 0) { fg = 0x00FFFFFF; bg = 0x00222244; }
            else if (val == 1) { /* bold - ignore for now */ }
            else if (val >= 30 && val <= 37) { fg = ansi_colors[val - 30]; }
            else if (val >= 40 && val <= 47) { bg = ansi_colors[val - 40]; }
            else if (val >= 90 && val <= 97) { fg = ansi_colors[8 + (val - 90)]; }
            else if (val >= 100 && val <= 107) { bg = ansi_colors[8 + (val - 100)]; }
            return 1;
        }
        // Unknown or incomplete sequence, skip
        while (*p && *p != 'm' && *p != '\033') p++;
        if (*p == 'm') { *s = p + 1; return 1; }
        return 0;
    }
    return 0;
}

void fbterm_init(uint32_t *framebuffer, int w, int h, int ox, int oy) {
    fb = framebuffer; term_w = w; term_h = h; fb_ox = ox; fb_oy = oy;
    cols = w / FONT_W; if (cols > 80) cols = 80;
    rows = h / FONT_H; if (rows > 48) rows = 48;
    // fb_w/fb_h are the actual framebuffer dimensions (set by display_init)
    cx = 0; cy = 0;
    fg = 0x00FFFFFF; bg = 0x00222244;
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
    if ((unsigned char)c < 32) {
        if (c == '\033') {
            const char *p = &c;
            ansi_parse(&p);
            dirty = 1;
        }
        return;
    }
    if (cx >= cols) { cx = 0; cy++; if (cy >= rows) fbterm_scroll(); }
    cells[cy][cx].c = c;
    cells[cy][cx].fg = fg;
    cells[cy][cx].bg = bg;
    cx++; dirty = 1;
}

void fbterm_write(const char *s) {
    while (*s) {
        if (*s == '\033') {
            ansi_parse(&s);
            continue;
        }
        fbterm_putchar(*s++);
    }
}

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
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            cell_t *cell = &cells[r][c];
            const uint8_t *glyph = font8x16_basic[(unsigned char)cell->c];
            int bx = fb_ox + c * FONT_W, by = fb_oy + r * FONT_H;
            int by_end = fb_oy + term_h;
            int bx_end = fb_ox + term_w;
            for (int row = 0; row < FONT_H && (by + row) < by_end && (by + row) < fb_h; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < FONT_W && (bx + col) < bx_end && (bx + col) < fb_w; col++)
                    fb[(by + row) * fb_w + (bx + col)] = (bits & (0x80 >> col)) ? cell->fg : cell->bg;
            }
        }
    }
    dirty = 0;
}

void fbterm_set_fg(uint32_t c) { fg = c; }
void fbterm_set_bg(uint32_t c) { bg = c; }
