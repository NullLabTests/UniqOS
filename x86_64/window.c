#include "window.h"
#include "display.h"
#include "kernel.h"
#include "font.h"

static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int next_id = 1;
static int focused_id = 0;
static int z_order[MAX_WINDOWS];

static int fb_w = 1024, fb_h = 768;
static uint32_t *fb_buf = 0;

static void draw_close_btn(uint32_t *fb, int cx, int cy) {
    for (int row = 0; row < BTN_H; row++)
        for (int col = 0; col < BTN_W; col++) {
            int px = cx + col, py = cy + row;
            if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                fb[py * fb_w + px] = 0x00CC3333;
        }
    uint32_t wc = 0xFFFFFFFF;
    for (int i = 3; i < 11; i++) {
        if (cx+i < fb_w && cy+3 < fb_h) fb[(cy+3)*fb_w + (cx+i)] = wc;
        if (cx+i < fb_w && cy+10 < fb_h) fb[(cy+10)*fb_w + (cx+i)] = wc;
    }
    for (int i = 3; i < 11; i++) {
        if (cx+i < fb_w && cy+3+(i-3) < fb_h) fb[(cy+3+(i-3))*fb_w + (cx+i)] = wc;
    }
}

static void draw_max_btn(uint32_t *fb, int cx, int cy) {
    for (int row = 0; row < BTN_H; row++)
        for (int col = 0; col < BTN_W; col++) {
            int px = cx + col, py = cy + row;
            if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                fb[py * fb_w + px] = 0x00339933;
        }
    uint32_t wc = 0xFFFFFFFF;
    for (int i = 2; i < 12; i++) {
        if (cx+i < fb_w && cy+2 < fb_h) fb[(cy+2)*fb_w + (cx+i)] = wc;
        if (cx+i < fb_w && cy+11 < fb_h) fb[(cy+11)*fb_w + (cx+i)] = wc;
    }
    for (int i = 2; i < 12; i++) {
        if (cx+2 < fb_w && cy+i < fb_h) fb[(cy+i)*fb_w + (cx+2)] = wc;
        if (cx+11 < fb_w && cy+i < fb_h) fb[(cy+i)*fb_w + (cx+11)] = wc;
    }
}

static void draw_min_btn(uint32_t *fb, int cx, int cy) {
    for (int row = 0; row < BTN_H; row++)
        for (int col = 0; col < BTN_W; col++) {
            int px = cx + col, py = cy + row;
            if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                fb[py * fb_w + px] = 0x00CCAA00;
        }
    uint32_t wc = 0xFFFFFFFF;
    for (int i = 2; i < 12; i++) {
        if (cx+i < fb_w && cy+10 < fb_h) fb[(cy+10)*fb_w + (cx+i)] = wc;
    }
}

static void window_draw_titlebar(window_t *w) {
    uint32_t *fb = fb_buf;
    int y = w->y;
    uint32_t bg = w->has_focus ? 0x00224488 : 0x00333355;
    for (int row = 0; row < w->titlebar_h; row++)
        for (int col = 0; col < w->w; col++) {
            int px = w->x + col, py = w->y + row;
            if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                fb[py * fb_w + px] = bg;
        }

    extern const uint8_t font8x16_basic[128][16];
    for (int i = 0; w->title[i] && i < (w->w - 60) / 8; i++) {
        const uint8_t *glyph = font8x16_basic[(unsigned char)w->title[i]];
        int bx = w->x + 4 + i * 8, by = y + 5;
        for (int row = 0; row < 14 && (by + row) < fb_h; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8 && (bx + col) < fb_w; col++)
                if (bits & (0x80 >> col))
                    fb[(by + row) * fb_w + (bx + col)] = 0xFFFFFFFF;
        }
    }

    int btn_y = y + 5;
    draw_min_btn(fb, w->x + w->w - 50, btn_y);
    draw_max_btn(fb, w->x + w->w - 33, btn_y);
    draw_close_btn(fb, w->x + w->w - 16, btn_y);
}

static void window_draw_border(window_t *w) {
    uint32_t *fb = fb_buf;
    for (int r = 0; r < WINDOW_BORDER; r++) {
        int bright = w->has_focus ? (160 - r * 20) : (100 - r * 10);
        if (bright < 30) bright = 30;
        int rb = bright * 160 / 255, gb = bright * 190 / 255, bb = bright * 255 / 255;
        if (rb > 255) rb = 255; if (gb > 255) gb = 255; if (bb > 255) bb = 255;
        uint32_t border = (rb << 16) | (gb << 8) | bb;
        int y1 = w->y - r, y2 = w->y + w->h - 1 + r;
        int x1 = w->x - r, x2 = w->x + w->w - 1 + r;
        for (int x = x1; x <= x2; x++) {
            if (x >= 0 && x < fb_w && y1 >= 0 && y1 < fb_h) fb[y1 * fb_w + x] = border;
            if (x >= 0 && x < fb_w && y2 >= 0 && y2 < fb_h) fb[y2 * fb_w + x] = border;
        }
        for (int y = y1; y <= y2; y++) {
            if (x1 >= 0 && x1 < fb_w && y >= 0 && y < fb_h) fb[y * fb_w + x1] = border;
            if (x2 >= 0 && x2 < fb_w && y >= 0 && y < fb_h) fb[y * fb_w + x2] = border;
        }
    }
}

void window_init(void) {
    fb_buf = display_get_buffer();
    fb_w = display_get_width();
    fb_h = display_get_height();
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].visible = 0;
        z_order[i] = 0;
    }
}

int window_create(int x, int y, int w, int h, const char *title) {
    if (window_count >= MAX_WINDOWS) return -1;
    int id = next_id++;
    window_t *win = &windows[window_count];
    win->id = id;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->restore_x = x; win->restore_y = y; win->restore_w = w; win->restore_h = h;
    win->titlebar_h = TITLEBAR_H;
    int i = 0; for (; title[i] && i < 63; i++) win->title[i] = title[i];
    win->title[i] = 0;
    win->visible = 1;
    win->minimized = 0;
    win->maximized = 0;
    win->has_focus = 0;
    win->drag_mode = 0;
    win->resize_mode = 0;
    win->bg_color = 0x001C1C3A;
    win->title_color = 0x003366CC;
    win->user_data = 0;
    win->draw = 0;
    win->on_key = 0;
    win->on_click = 0;
    win->on_close = 0;
    focused_id = id;
    z_order[window_count] = id;
    window_count++;
    return id;
}

void window_close(int id) {
    window_t *win = window_get(id);
    if (!win) return;
    if (win->on_close) win->on_close(win);
    win->visible = 0;
    if (focused_id == id) {
        focused_id = 0;
        for (int i = window_count - 1; i >= 0; i--)
            if (windows[i].visible && windows[i].id != id) { focused_id = windows[i].id; break; }
    }
}

void window_focus(int id) {
    focused_id = id;
    for (int i = 0; i < window_count; i++)
        windows[i].has_focus = (windows[i].id == id);
    window_zorder_top(id);
}

void window_zorder_top(int id) {
    int zi = -1;
    for (int i = 0; i < window_count; i++) if (z_order[i] == id) { zi = i; break; }
    if (zi >= 0) {
        int tmp = z_order[zi];
        for (int i = zi; i < window_count - 1; i++) z_order[i] = z_order[i + 1];
        z_order[window_count - 1] = tmp;
    }
}

window_t *window_get(int id) {
    for (int i = 0; i < window_count; i++)
        if (windows[i].id == id && windows[i].visible) return &windows[i];
    return 0;
}

window_t *window_get_by_userdata(void *data) {
    for (int i = 0; i < window_count; i++)
        if (windows[i].visible && windows[i].user_data == data) return &windows[i];
    return 0;
}

void window_set_draw(int id, void (*draw)(window_t *)) {
    window_t *win = window_get(id); if (win) win->draw = draw;
}
void window_set_onkey(int id, void (*onkey)(window_t *, char)) {
    window_t *win = window_get(id); if (win) win->on_key = onkey;
}
void window_set_onclick(int id, int (*onclick)(window_t *, int, int)) {
    window_t *win = window_get(id); if (win) win->on_click = onclick;
}
void window_set_onclose(int id, void (*onclose)(window_t *)) {
    window_t *win = window_get(id); if (win) win->on_close = onclose;
}

window_t *window_get_dragging(void) {
    for (int i = 0; i < window_count; i++)
        if (windows[i].visible && windows[i].drag_mode) return &windows[i];
    return 0;
}

void window_set_title(int id, const char *title) {
    window_t *win = window_get(id);
    if (!win) return;
    int i = 0; for (; title[i] && i < 63; i++) win->title[i] = title[i];
    win->title[i] = 0;
}

void window_move(int id, int x, int y) {
    window_t *win = window_get(id);
    if (win) { win->x = x; win->y = y; }
}

void window_resize(int id, int w, int h) {
    window_t *win = window_get(id);
    if (win) { if (w > 200) win->w = w; if (h > 100) win->h = h; }
}

void window_toggle_minimize(int id) {
    window_t *win = window_get(id);
    if (!win) return;
    win->minimized = !win->minimized;
    if (win->minimized && focused_id == id) {
        focused_id = 0;
        for (int i = window_count - 1; i >= 0; i--)
            if (windows[i].visible && !windows[i].minimized && windows[i].id != id) { focused_id = windows[i].id; break; }
    }
    if (!win->minimized) window_focus(id);
}

void window_toggle_maximize(int id) {
    window_t *win = window_get(id);
    if (!win) return;
    if (!win->maximized) {
        win->restore_x = win->x; win->restore_y = win->y;
        win->restore_w = win->w; win->restore_h = win->h;
        win->x = 0; win->y = 0;
        win->w = fb_w; win->h = fb_h;
        win->maximized = 1;
    } else {
        win->x = win->restore_x; win->y = win->restore_y;
        win->w = win->restore_w; win->h = win->restore_h;
        win->maximized = 0;
    }
}

int window_get_count(void) { return window_count; }
int window_get_id(int index) { return (index >= 0 && index < window_count) ? windows[index].id : -1; }
int window_is_minimized(int id) { window_t *w = window_get(id); return w ? w->minimized : 0; }

void window_render_all(void) {
    for (int zi = 0; zi < window_count; zi++) {
        int id = z_order[zi];
        window_t *w = window_get(id);
        if (!w) continue;
        uint32_t *fb = fb_buf;

        if (w->minimized) continue;

        for (int row = 0; row < w->h; row++)
            for (int col = 0; col < w->w; col++) {
                int px = w->x + col, py = w->y + row;
                if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                    fb[py * fb_w + px] = w->bg_color;
            }

        window_draw_titlebar(w);

        if (w->draw) w->draw(w);

        window_draw_border(w);
    }
}

int window_handle_click(int mx, int my, int buttons) {
    (void)buttons;
    for (int zi = window_count - 1; zi >= 0; zi--) {
        int id = z_order[zi];
        window_t *w = window_get(id);
        if (!w || w->minimized) continue;
        if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) {
            window_focus(w->id);

            if (my < w->y + w->titlebar_h) {
                if (mx >= w->x + w->w - 16 && mx < w->x + w->w) {
                    if (w->on_close) w->on_close(w);
                    window_close(w->id);
                    return 1;
                }
                if (mx >= w->x + w->w - 33 && mx < w->x + w->w - 33 + BTN_W) {
                    window_toggle_maximize(w->id);
                    return 1;
                }
                if (mx >= w->x + w->w - 50 && mx < w->x + w->w - 50 + BTN_W) {
                    window_toggle_minimize(w->id);
                    return 1;
                }
                w->drag_mode = 1;
                w->drag_off_x = mx - w->x;
                w->drag_off_y = my - w->y;
            } else if (mx >= w->x + w->w - 12 && my >= w->y + w->h - 12) {
                w->resize_mode = 1;
                w->resize_edge = 3;
            } else {
                if (w->on_click) w->on_click(w, mx, my);
            }
            return 1;
        }
    }
    focused_id = 0;
    return 0;
}

int window_handle_key(char c) {
    window_t *win = window_get(focused_id);
    if (win && win->on_key) {
        win->on_key(win, c);
        return 1;
    }
    return 0;
}
