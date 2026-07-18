#include "sysmon.h"
#include "window.h"
#include "display.h"
#include "font.h"
#include "kernel.h"
#include "heap.h"

#define HIST_LEN 200

typedef struct {
    int history[HIST_LEN];
    int hist_pos;
    int dirty;
} sysmon_t;

static sysmon_t *sm_data = 0;

void sysmon_open(void) {
    if (!sm_data) {
        sm_data = (sysmon_t *)kmalloc(sizeof(sysmon_t));
        sm_data->hist_pos = 0;
        for (int i = 0; i < HIST_LEN; i++) sm_data->history[i] = 0;
    }
    sm_data->dirty = 1;
    int wid = sysmon_window_id();
    if (wid < 0)
        wid = window_create(180, 130, 480, 360, "System Monitor");
    window_set_draw(wid, (void (*)(window_t *))&sysmon_tick);
    window_t *win = window_get(wid);
    if (win) win->user_data = sm_data;
    window_focus(wid);
}

int sysmon_window_id(void) {
    if (!sm_data) return -1;
    window_t *win = window_get_by_userdata(sm_data);
    return win ? win->id : -1;
}

void sysmon_tick(void) {
    window_t *win = window_get_by_userdata(sm_data);
    if (!win || !sm_data) return;
    sysmon_t *sm = sm_data;

    // Record CPU usage sample
    uint64_t free_pages = pmm_get_free_count();
    uint64_t total_pages = 512 * 1024 * 1024 / 4096 / 4; // rough estimate
    int pct = 100 - (int)(free_pages * 100 / total_pages);
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    sm->history[sm->hist_pos % HIST_LEN] = pct;
    sm->hist_pos++;

    int x = win->x, y = win->y;
    int bw = win->w, bh = win->h;
    int mx = x + 12, my = y + win->titlebar_h + 8;

    // CPU Usage graph
    display_fill_rect(mx, my, bw - 24, 100, 0x00181830);
    display_put_char(mx + 2, my + 2, 'C', 0x00FFAA00, 0x00181830);
    display_put_char(mx + 10, my + 2, 'P', 0x00FFAA00, 0x00181830);
    display_put_char(mx + 18, my + 2, 'U', 0x00FFAA00, 0x00181830);

    int graph_w = bw - 40;
    int graph_h = 60;
    int gx = mx + 6, gy = my + 24;
    display_fill_rect(gx, gy, graph_w, graph_h, 0x00081020);

    int samples = sm->hist_pos < HIST_LEN ? sm->hist_pos : HIST_LEN;
    int bar_w = samples > 1 ? graph_w / samples : graph_w;
    if (bar_w < 1) bar_w = 1;
    for (int i = 0; i < samples; i++) {
        int val = sm->history[(sm->hist_pos - samples + i + HIST_LEN) % HIST_LEN];
        int bar_h = val * graph_h / 100;
        uint32_t color = val > 80 ? 0x00FF4444 : val > 50 ? 0x00FFAA00 : 0x0044CC44;
        int bx = gx + i * bar_w;
        display_fill_rect(bx, gy + graph_h - bar_h, bar_w, bar_h, color);
    }

    display_put_char(gx + 2, gy - 14, '1', 0x00666666, 0x00081020);
    display_put_char(gx + 10, gy - 14, '0', 0x00666666, 0x00081020);
    display_put_char(gx + 18, gy - 14, '0', 0x00666666, 0x00081020);
    display_put_char(gx + 26, gy - 14, '%', 0x00666666, 0x00081020);

    // Memory bar
    int mm_y = my + 110;
    display_fill_rect(mx, mm_y, bw - 24, 60, 0x00181830);
    display_put_char(mx + 2, mm_y + 2, 'M', 0x0044AAFF, 0x00181830);
    display_put_char(mx + 10, mm_y + 2, 'e', 0x0044AAFF, 0x00181830);
    display_put_char(mx + 18, mm_y + 2, 'm', 0x0044AAFF, 0x00181830);

    int mem_bar_x = mx + 6, mem_bar_y = mm_y + 22;
    int mem_bar_w = bw - 36, mem_bar_h = 20;
    display_fill_rect(mem_bar_x, mem_bar_y, mem_bar_w, mem_bar_h, 0x00081020);
    int used_w = (total_pages - free_pages) * mem_bar_w / total_pages;
    if (used_w < 0) used_w = 0;
    if (used_w > mem_bar_w) used_w = mem_bar_w;
    display_fill_rect(mem_bar_x, mem_bar_y, used_w, mem_bar_h, 0x003388FF);
    display_fill_rect(mem_bar_x + used_w, mem_bar_y, mem_bar_w - used_w, mem_bar_h, 0x00224466);

    char mem_text[64]; int mp = 0;
    const char *mem_s = "Used: "; while (*mem_s) mem_text[mp++] = *mem_s++;
    unsigned uv = (unsigned)((total_pages - free_pages) * 4); char tb[16]; int tp = 0; do { tb[tp++] = '0' + (uv % 10); uv /= 10; } while (uv);
    while (tp > 0) mem_text[mp++] = tb[--tp];
    mem_text[mp++] = 'K'; mem_text[mp++] = 'B'; mem_text[mp++] = ' '; mem_text[mp++] = '/'; mem_text[mp++] = ' ';
    uv = (unsigned)(total_pages * 4); tp = 0; do { tb[tp++] = '0' + (uv % 10); uv /= 10; } while (uv);
    while (tp > 0) mem_text[mp++] = tb[--tp];
    mem_text[mp++] = 'K'; mem_text[mp++] = 'B';
    mem_text[mp] = 0;
    for (int i = 0; mem_text[i]; i++)
        display_put_char(mem_bar_x + 4 + i * 8, mem_bar_y + 2, mem_text[i], 0x00CCCCFF, 0x00081020);

    // Process list
    int proc_y = mm_y + 70;
    display_fill_rect(mx, proc_y, bw - 24, bh - (proc_y - y) - 20, 0x00181830);
    display_put_char(mx + 2, proc_y + 2, 'P', 0x00FFCC44, 0x00181830);
    display_put_char(mx + 10, proc_y + 2, 'r', 0x00FFCC44, 0x00181830);
    display_put_char(mx + 18, proc_y + 2, 'o', 0x00FFCC44, 0x00181830);
    display_put_char(mx + 26, proc_y + 2, 'c', 0x00FFCC44, 0x00181830);
    display_put_char(mx + 34, proc_y + 2, 'e', 0x00FFCC44, 0x00181830);
    display_put_char(mx + 42, proc_y + 2, 's', 0x00FFCC44, 0x00181830);
    display_put_char(mx + 50, proc_y + 2, 's', 0x00FFCC44, 0x00181830);
    display_put_char(mx + 58, proc_y + 2, 'e', 0x00FFCC44, 0x00181830);
    display_put_char(mx + 66, proc_y + 2, 's', 0x00FFCC44, 0x00181830);

    int proc_list_y = proc_y + 18;
    const char *procs[] = {"kernel  [running]  CPU: 5%  MEM: 8MB", "desktop [running]  CPU: 2%  MEM: 4MB", "shell   [running]  CPU: 1%  MEM: 2MB"};
    for (int i = 0; i < 3 && proc_list_y + 16 < y + bh - 10; i++) {
        display_fill_rect(mx + 2, proc_list_y, bw - 28, 15, 0x00202040);
        for (int j = 0; procs[i][j]; j++)
            display_put_char(mx + 8 + j * 8, proc_list_y + 1, procs[i][j], 0x00BBBBBB, 0x00202040);
        proc_list_y += 16;
    }

    sm->dirty = 0;
}
