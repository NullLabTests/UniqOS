#include "fileman.h"
#include "window.h"
#include "display.h"
#include "font.h"
#include "vfs.h"
#include "timefmt.h"
#include "kernel.h"
#include "heap.h"

#define ROW_H 20
#define HEADER_H 22
#define STATUS_H 18
#define SIDEBAR_W 140

typedef struct {
    char path[256];
    int scroll;
    int sel;
    int count;
    vnode_t entries[128];
    int dirty;
} fileman_t;

static fileman_t *fm_data = 0;

static void fileman_refresh(fileman_t *fm) {
    fm->count = vfs_list(fm->path, fm->entries, 128);
    if (fm->count < 0) fm->count = 0;
    fm->sel = 0;
    fm->scroll = 0;
    fm->dirty = 1;
}

void fileman_open(const char *path) {
    if (!fm_data) fm_data = (fileman_t *)kmalloc(sizeof(fileman_t));
    fileman_t *fm = fm_data;
    int i = 0; const char *p = path ? path : "/home/user";
    for (; *p && i < 255; i++) fm->path[i] = *p++;
    fm->path[i] = 0;
    fileman_refresh(fm);
    int wid = fileman_window_id();
    if (wid < 0) {
        wid = window_create(200, 80, 640, 400, "File Manager");
    }
    window_set_title(wid, "File Manager");
    window_t *win = window_get(wid);
    if (win) win->user_data = fm;
    window_focus(wid);
    fm->dirty = 1;
}

int fileman_window_id(void) {
    if (!fm_data) return -1;
    window_t *win = window_get_by_userdata(fm_data);
    return win ? win->id : -1;
}

void fileman_tick(void) {
    window_t *win = window_get_by_userdata(fm_data);
    if (!win || !fm_data) return;
    fileman_t *fm = fm_data;
    if (!fm->dirty) return;

    int x = win->x, y = win->y;
    int bw = win->w, bh = win->h;

    int sidebar_end = x + SIDEBAR_W;
    int list_x = sidebar_end + 4;
    int list_w = bw - (SIDEBAR_W + 8);
    int list_y = y + HEADER_H;
    int list_h = bh - HEADER_H - STATUS_H;
    int max_rows = list_h / ROW_H;

    // Sidebar
    display_fill_rect(x + 1, y + HEADER_H, SIDEBAR_W - 2, bh - HEADER_H - STATUS_H, 0x00202040);
    const char *places[] = {"Home","Desktop","Documents","tmp","/"};
    int py = y + HEADER_H + 4;
    for (int i = 0; i < 5; i++) {
        display_fill_rect(x + 2, py, SIDEBAR_W - 4, ROW_H, 0x00303060);
        for (int j = 0; places[i][j]; j++)
            display_put_char(x + 10 + j * 8, py + 2, places[i][j], 0x00BBBBFF, 0x00303060);
        py += ROW_H;
    }
    py += 8;
    display_put_char(x + 10, py, 'D', 0x007788AA, 0x00202040);
    display_put_char(x + 18, py, 'r', 0x007788AA, 0x00202040);
    display_put_char(x + 26, py, 'i', 0x007788AA, 0x00202040);
    display_put_char(x + 34, py, 'v', 0x007788AA, 0x00202040);
    display_put_char(x + 42, py, 'e', 0x007788AA, 0x00202040);
    display_put_char(x + 50, py, 's', 0x007788AA, 0x00202040);

    // Header
    display_fill_rect(x + 1, y + 1, bw - 2, HEADER_H - 1, 0x00303050);
    const char *hdr[] = {"Name", "Size", "Date"};
    int hx = list_x;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; hdr[i][j]; j++)
            display_put_char(hx + j * 8, y + 3, hdr[i][j], 0x00CCCCFF, 0x00303050);
        hx += (i == 0) ? 240 : (i == 1) ? 80 : 160;
    }

    // File list
    int start = fm->scroll;
    int end = start + max_rows;
    int ry = list_y;
    for (int i = start; i < end && i < fm->count; i++) {
        vnode_t *e = &fm->entries[i];
        uint32_t row_bg = (i == fm->sel) ? 0x00335588 : (i % 2 == 0) ? 0x00222244 : 0x001C1C3A;
        display_fill_rect(list_x, ry, list_w, ROW_H, row_bg);

        uint32_t name_color = e->is_dir ? 0x00AADDFF : 0x00DDDDDD;
        int col = 0;
        if (e->is_dir) {
            display_put_char(list_x, ry + 2, '[', name_color, row_bg);
            col = 1;
        }
        for (int j = 0; e->name[j] && j < 28; j++)
            display_put_char(list_x + (col + j) * 8, ry + 2, e->name[j], name_color, row_bg);
        if (e->is_dir)
            display_put_char(list_x + (col + (int)strlen(e->name)) * 8, ry + 2, ']', name_color, row_bg);

        char sz[16]; int sp = 0;
        unsigned vs = e->size; do { sz[sp++] = '0' + (vs % 10); vs /= 10; } while (vs);
        int sx = list_x + 248;
        for (int j = sp - 1; j >= 0; j--) { display_put_char(sx, ry + 2, sz[j], 0x009999AA, row_bg); sx += 8; }

        const char *fd = timefmt_filedate(e->modified);
        int dx = list_x + 320;
        for (int j = 0; fd[j]; j++) { display_put_char(dx, ry + 2, fd[j], 0x009999AA, row_bg); dx += 8; }

        ry += ROW_H;
    }

    // Status bar
    int st_y = y + bh - STATUS_H;
    display_fill_rect(x + 1, st_y, bw - 2, STATUS_H - 1, 0x00202040);
    char status[64];
    int sp2 = 0;
    const char *cp = fm->path; while (*cp) status[sp2++] = *cp++;
    status[sp2++] = ' '; 
    char cnt[8]; int cp2 = 0; unsigned cc = fm->count; do { cnt[cp2++] = '0' + (cc % 10); cc /= 10; } while (cc);
    while (cp2 > 0) status[sp2++] = cnt[--cp2];
    status[sp2++] = ' '; status[sp2++] = 'i'; status[sp2++] = 't'; status[sp2++] = 'e'; status[sp2++] = 'm'; status[sp2++] = 's';
    status[sp2] = 0;
    for (int i = 0; status[i]; i++)
        display_put_char(x + 6 + i * 8, st_y + 1, status[i], 0x009999BB, 0x00202040);

    fm->dirty = 0;


}
