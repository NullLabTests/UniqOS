#include "editor.h"
#include "window.h"
#include "display.h"
#include "font.h"
#include "vfs.h"
#include "kernel.h"
#include "heap.h"

#define EDIT_LINES 256
#define EDIT_COLS 128
#define GUTTER_W 40

typedef struct {
    char path[256];
    char lines[EDIT_LINES][EDIT_COLS];
    int line_count;
    int cx, cy;
    int scroll_x, scroll_y;
    int dirty;
    int modified;
} editor_t;

static editor_t *ed_data = 0;

void editor_open(const char *path) {
    if (!ed_data) {
        ed_data = (editor_t *)kmalloc(sizeof(editor_t));
        ed_data->path[0] = 0;
        ed_data->line_count = 1;
        ed_data->lines[0][0] = 0;
        ed_data->cx = 0; ed_data->cy = 0;
        ed_data->scroll_x = 0; ed_data->scroll_y = 0;
        ed_data->modified = 0;
    }
    editor_t *ed = ed_data;
    if (path) {
        int i = 0; for (; *path && i < 255; i++) ed->path[i] = *path++;
        ed->path[i] = 0;
        char buf[8192];
        int r = vfs_read(ed->path, buf, 8191);
        if (r > 0) {
            buf[r] = 0;
            ed->line_count = 0;
            int lp = 0;
            ed->lines[0][0] = 0;
            for (int j = 0; j < r && ed->line_count < EDIT_LINES; j++) {
                if (buf[j] == '\n') {
                    ed->lines[ed->line_count][lp] = 0;
                    ed->line_count++;
                    lp = 0;
                } else if (lp < EDIT_COLS - 1) {
                    ed->lines[ed->line_count][lp++] = buf[j];
                }
            }
            ed->lines[ed->line_count][lp] = 0;
            if (buf[r-1] != '\n' || ed->line_count == 0) ed->line_count++;
            ed->modified = 0;
        }
    }
    ed->cx = 0; ed->cy = 0;
    ed->scroll_x = 0; ed->scroll_y = 0;
    ed->dirty = 1;

    int wid = editor_window_id();
    if (wid < 0)
        wid = window_create(250, 70, 650, 400, "Text Editor");
    window_set_title(wid, path ? path : "untitled");

    window_t *win = window_get(wid);
    if (win) win->user_data = ed;
    window_focus(wid);
}

int editor_window_id(void) {
    if (!ed_data) return -1;
    window_t *win = window_get_by_userdata(ed_data);
    return win ? win->id : -1;
}

void editor_tick(void) {
    window_t *win = 0;
    if (ed_data) win = window_get_by_userdata(ed_data);
    if (!win || !ed_data) return;
    editor_t *ed = ed_data;

    ed->dirty = 1;

    int x = win->x, y = win->y;
    int bw = win->w, bh = win->h;
    int text_x = x + GUTTER_W + 4;
    int text_y = y + win->titlebar_h + 2;
    int text_w = bw - GUTTER_W - 8;
    int text_h = bh - win->titlebar_h - 22;
    int max_cols = text_w / 8;
    int max_rows = text_h / 16;

    // Gutter
    for (int row = 0; row < max_rows && row + ed->scroll_y < ed->line_count; row++) {
        int ln = row + ed->scroll_y + 1;
        if (ln == ed->cy + 1) {
            display_fill_rect(x + 1, text_y + row * 16, GUTTER_W, 16, 0x00335577);
        } else {
            display_fill_rect(x + 1, text_y + row * 16, GUTTER_W, 16, 0x00202040);
        }
        unsigned v = ln; char tn[8]; int tp = 0; do { tn[tp++] = '0' + (v % 10); v /= 10; } while (v);
        int gn = GUTTER_W - 8;
        for (int j = tp - 1; j >= 0; j--) { display_put_char(x + gn, text_y + row * 16, tn[j], 0x007788AA, (ln == ed->cy+1) ? 0x00335577 : 0x00202040); gn -= 8; }
    }

    // Text area
    for (int row = 0; row < max_rows && row + ed->scroll_y < ed->line_count; row++) {
        int l = row + ed->scroll_y;
        int is_cursor_line = (l == ed->cy);
        uint32_t bg = is_cursor_line ? 0x00224466 : 0x001C1C3A;
        display_fill_rect(text_x, text_y + row * 16, text_w, 16, bg);
        for (int col = 0; col < max_cols && col + ed->scroll_x < (int)strlen(ed->lines[l]); col++) {
            char c = ed->lines[l][col + ed->scroll_x];
            uint32_t fg = is_cursor_line ? 0x00FFFFFF : 0x00CCCCCC;
            if (ed->cx == col + ed->scroll_x && ed->cy == l && (timer_get_ms() / 500) % 2)
                display_fill_rect(text_x + col * 8, text_y + row * 16, 8, 16, 0x00FFFFFF);
            display_put_char(text_x + col * 8, text_y + row * 16, c, fg, bg);
        }
        if (ed->cx >= (int)strlen(ed->lines[l]) && ed->cy == l && (timer_get_ms() / 500) % 2) {
            int ccol = (int)strlen(ed->lines[l]) - ed->scroll_x;
            if (ccol >= 0 && ccol < max_cols)
                display_fill_rect(text_x + ccol * 8, text_y + row * 16, 8, 16, 0x00FFFFFF);
        }
    }

    // Status
    int st_y = y + bh - 18;
    display_fill_rect(x + 1, st_y, bw - 2, 17, 0x00202040);
    char st[64]; int sp = 0;
    const char *fn = ed->path[0] ? ed->path : "untitled";
    while (*fn) st[sp++] = *fn++;
    st[sp++] = ' '; st[sp++] = '-'; st[sp++] = ' ';
    unsigned lc = ed->cy + 1; char tc[8]; int tp = 0; do { tc[tp++] = '0' + (lc % 10); lc /= 10; } while (lc);
    while (tp > 0) st[sp++] = tc[--tp];
    st[sp++] = ':';
    unsigned cc = ed->cx + 1; tp = 0; do { tc[tp++] = '0' + (cc % 10); cc /= 10; } while (cc);
    while (tp > 0) st[sp++] = tc[--tp];
    if (ed->modified) { st[sp++] = ' '; st[sp++] = '*'; }
    st[sp] = 0;
    for (int i = 0; st[i]; i++) display_put_char(x + 6 + i * 8, st_y + 1, st[i], 0x009999BB, 0x00202040);

    ed->dirty = 0;
}
