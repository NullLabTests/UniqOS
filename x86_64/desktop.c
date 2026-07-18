#include "desktop.h"
#include "window.h"
#include "display.h"
#include "fbterm.h"
#include "mouse.h"
#include "keyboard.h"
#include "kernel.h"
#include "font.h"
#include "tcp.h"
#include "http.h"
#include "browser.h"
#include "shell.h"
#include "fileman.h"
#include "editor.h"
#include "sysmon.h"
#include "timefmt.h"
#include "vfs.h"
#include "virtio_net.h"
#include "serial.h"

#define TOPBAR_H 28
#define DESKTOP_ICON_SIZE 64
#define ICON_GAP 16

static int fb_w = 1024, fb_h = 768;
static uint32_t *fb_buf = 0;

static int mouse_x = 0, mouse_y = 0, mouse_buttons = 0;
static int prev_mouse_buttons = 0;

static int shell_win_id = 0;
static int context_menu_open = 0;
static int context_menu_x = 0, context_menu_y = 0;

static const char *ico_names[] = {"Home","Terminal","Files","Editor","Monitor","Browser"};
static const char *ico_paths[] = {"/home/user","","","","",""};
static const char *ico_icons[] = {"H","T","F","E","M","W"};
static const uint32_t ico_colors[] = {0x0044AAFF,0x0033DD33,0x00FFAA00,0x00FF66AA,0x00FF4444,0x008888FF};

static int active_launchers[6];
static int ico_count = 6;

static void draw_background(void) {
    for (int y = TOPBAR_H; y < fb_h; y++) {
        uint32_t c;
        if (y < fb_h / 3) {
            int t = (y - TOPBAR_H) * 255 / (fb_h / 3);
            int r = 20 + t * 4 / 255;
            int g = 30 + t * 20 / 255;
            int b = 50 + t * 30 / 255;
            c = 0x00000000 | (r << 16) | (g << 8) | b;
        } else {
            c = 0x001C1C3A;
        }
        for (int x = 0; x < fb_w; x++)
            fb_buf[y * fb_w + x] = c;
    }
}

static void draw_topbar(void) {
    for (int y = 0; y < TOPBAR_H; y++)
        for (int x = 0; x < fb_w; x++)
            fb_buf[y * fb_w + x] = 0x00222233;

    for (int y = TOPBAR_H - 1; y < TOPBAR_H; y++)
        for (int x = 0; x < fb_w; x++)
            fb_buf[y * fb_w + x] = 0x00444466;

    extern const uint8_t font8x16_basic[128][16];

    // Activities button
    int ax = 8;
    const char *act = "Activities";
    display_fill_rect(4, 4, 80, TOPBAR_H - 8, 0x00333355);
    for (int i = 0; act[i]; i++) {
        const uint8_t *g = font8x16_basic[(unsigned char)act[i]];
        int bx = ax + i * 8, by = 7;
        for (int row = 0; row < 14 && by + row < fb_h; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8 && bx + col < fb_w; col++)
                if (bits & (0x80 >> col))
                    fb_buf[(by + row) * fb_w + (bx + col)] = 0x00AABBFF;
        }
    }

    // Launcher icons in top bar
    int lx = 90;
    for (int i = 0; i < ico_count; i++) {
        int ico_w = 24;
        display_fill_rect(lx, 2, ico_w, TOPBAR_H - 4, active_launchers[i] ? 0x00335577 : 0x002A2A44);
        int cd = ico_icons[i][0];
        if (cd >= 'A' && cd <= 'Z') cd = cd - 'A' + 'a';
        const uint8_t *g = font8x16_basic[(unsigned char)cd];
        int cx2 = lx + (ico_w - 8) / 2, cy2 = 6;
        for (int row = 0; row < 14; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; col++)
                if (bits & (0x80 >> col))
                    fb_buf[(cy2 + row) * fb_w + (cx2 + col)] = ico_colors[i];
        }
        lx += ico_w + 2;
    }

    // Window buttons in top bar
    int wx2 = lx + 8;
    for (int i = 0; i < window_get_count(); i++) {
        int wid = window_get_id(i);
        window_t *w = window_get(wid);
        if (!w || w->minimized) continue;
        int wb_w = (strlen(w->title) * 8 + 16);
        if (wb_w > 150) wb_w = 150;
        if (wb_w < 60) wb_w = 60;
        display_fill_rect(wx2, 2, wb_w, TOPBAR_H - 4, w->has_focus ? 0x00335577 : 0x002A2A44);
        for (int j = 0; w->title[j] && j < 16; j++) {
            const uint8_t *g = font8x16_basic[(unsigned char)w->title[j]];
            int bx3 = wx2 + 6 + j * 8, by3 = 6;
            for (int row = 0; row < 14 && by3 + row < fb_h; row++) {
                uint8_t bits = g[row];
                for (int col = 0; col < 8 && bx3 + col < fb_w; col++)
                    if (bits & (0x80 >> col))
                        fb_buf[(by3 + row) * fb_w + (bx3 + col)] = w->has_focus ? 0x00FFFFFF : 0x008888AA;
            }
        }
        wx2 += wb_w + 2;
    }

    // Clock
    uint64_t ms = timer_get_ms();
    const char *time_str = timefmt_clock(ms);
    int tx = fb_w - 100;
    for (int i = 0; time_str[i]; i++) {
        const uint8_t *g = font8x16_basic[(unsigned char)time_str[i]];
        int bx = tx + i * 8, by = 6;
        for (int row = 0; row < 14; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; col++)
                if (bits & (0x80 >> col))
                    fb_buf[(by + row) * fb_w + (bx + col)] = 0x00FFFFFF;
        }
    }

    // Power button
    int px = fb_w - 26;
    display_fill_rect(px, 4, 22, TOPBAR_H - 8, 0x00553333);
    const uint8_t *pg = font8x16_basic[(int)'x'];
    int bx4 = px + 7, by4 = 6;
    for (int row = 0; row < 14; row++) {
        uint8_t bits = pg[row];
        for (int col = 0; col < 8; col++)
            if (bits & (0x80 >> col))
                fb_buf[(by4 + row) * fb_w + (bx4 + col)] = 0x00FF6666;
    }
}

static void draw_desktop_icons(void) {
    int start_x = 30;
    int start_y = TOPBAR_H + 30;
    int icos_per_row = (fb_w - start_x * 2) / (DESKTOP_ICON_SIZE + ICON_GAP);
    if (icos_per_row < 1) icos_per_row = 1;

    for (int i = 0; i < ico_count; i++) {
        int row = i / icos_per_row;
        int col = i % icos_per_row;
        int ix = start_x + col * (DESKTOP_ICON_SIZE + ICON_GAP);
        int iy = start_y + row * (DESKTOP_ICON_SIZE + 24);

        // Icon background (rounded rect feel)
        display_fill_rect(ix + 4, iy + 4, DESKTOP_ICON_SIZE - 8, DESKTOP_ICON_SIZE - 8, 0x002A2A55);
        display_fill_rect(ix + 6, iy + 6, DESKTOP_ICON_SIZE - 12, DESKTOP_ICON_SIZE - 12, ico_colors[i]);

        // Icon letter
        const uint8_t *g = font8x16_basic[(unsigned char)ico_icons[i][0]];
        int cx = ix + (DESKTOP_ICON_SIZE - 8) / 2;
        int cy = iy + (DESKTOP_ICON_SIZE - 16) / 2;
        for (int row = 0; row < 14; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; col++)
                if (bits & (0x80 >> col))
                    fb_buf[(cy + row) * fb_w + (cx + col)] = 0x00FFFFFF;
        }

        // Label
        int label_x = ix + (DESKTOP_ICON_SIZE - (int)strlen(ico_names[i]) * 8) / 2;
        if (label_x < 0) label_x = 0;
        for (int j = 0; ico_names[i][j]; j++) {
            const uint8_t *g2 = font8x16_basic[(unsigned char)ico_names[i][j]];
            for (int row = 0; row < 14; row++) {
                uint8_t bits = g2[row];
                for (int col = 0; col < 8; col++)
                    if (bits & (0x80 >> col))
                        fb_buf[(iy + DESKTOP_ICON_SIZE + 4 + row) * fb_w + (label_x + j * 8 + col)] = 0x00DDDDEE;
            }
        }
    }
}

static void draw_context_menu(void) {
    if (!context_menu_open) return;
    int mx = context_menu_x, my = context_menu_y;
    int mw = 160, mh = 140;
    if (mx + mw > fb_w) mx = fb_w - mw;
    if (my + mh > fb_h) my = fb_h - mh;

    display_fill_rect(mx, my, mw, mh, 0x00303050);
    for (int x = mx; x < mx + mw; x++) {
        fb_buf[my * fb_w + x] = 0x00555588;
        fb_buf[(my + mh - 1) * fb_w + x] = 0x00555588;
    }
    for (int y = my; y < my + mh; y++) {
        fb_buf[y * fb_w + mx] = 0x00555588;
        fb_buf[y * fb_w + mx + mw - 1] = 0x00555588;
    }

    const char *items[] = {"New Terminal","New File Manager","New Text Editor","System Monitor","Close All Windows"};
    for (int i = 0; i < 5; i++) {
        int iy = my + 4 + i * 26;
        display_fill_rect(mx + 2, iy, mw - 4, 24, 0x00383860);
        for (int j = 0; items[i][j]; j++)
            display_put_char(mx + 10 + j * 8, iy + 4, items[i][j], 0x00DDDDEE, 0x00383860);
    }
}

static void draw_mouse(void) {
    uint32_t white = 0xFFFFFFFF;
    int mw = 12, mh = 16;
    const uint8_t cursor[16] = {
        0xFF, 0xC0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0xFE, 0xF8, 0xF0, 0x60, 0x00, 0x00, 0x00
    };
    for (int row = 0; row < mh && mouse_y + row < fb_h; row++) {
        uint8_t bits = cursor[row];
        for (int col = 0; col < mw && mouse_x + col < fb_w; col++)
            if (bits & (0x80 >> col))
                fb_buf[(mouse_y + row) * fb_w + (mouse_x + col)] = white;
    }
}

static void term_draw(window_t *w) { (void)w; fbterm_render(); }
static void term_key(window_t *w, char c) { (void)w; shell_handle_char(c); }

static void launch_terminal(void) {
    window_t *win = window_get(shell_win_id);
    if (win && win->visible && !win->minimized) {
        window_focus(shell_win_id);
        return;
    }
    shell_win_id = window_create(80, 40, 700, 450, "Terminal");
    fbterm_init(display_get_buffer(), 680, 400);
    fbterm_write("Welcome to UniqOS Desktop!\n");
    fbterm_write(shell_prompt());
    window_set_draw(shell_win_id, term_draw);
    window_set_onkey(shell_win_id, term_key);
    window_focus(shell_win_id);
}

static int desktop_icon_at(int mx, int my) {
    int start_x = 30;
    int start_y = TOPBAR_H + 30;
    int icos_per_row = (fb_w - start_x * 2) / (DESKTOP_ICON_SIZE + ICON_GAP);
    if (icos_per_row < 1) icos_per_row = 1;
    for (int i = 0; i < ico_count; i++) {
        int row = i / icos_per_row;
        int col = i % icos_per_row;
        int ix = start_x + col * (DESKTOP_ICON_SIZE + ICON_GAP);
        int iy = start_y + row * (DESKTOP_ICON_SIZE + 24);
        if (mx >= ix && mx < ix + DESKTOP_ICON_SIZE && my >= iy && my < iy + DESKTOP_ICON_SIZE + 20)
            return i;
    }
    return -1;
}

static void handle_launcher_click(int mx) {
    int lx = 90;
    for (int i = 0; i < ico_count; i++) {
        int ico_w = 24;
        if (mx >= lx && mx < lx + ico_w) {
            if (i == 1) launch_terminal();
            else {
                if (i == 0 || i == 2) fileman_open(ico_paths[i]);
                else if (i == 3) editor_open(0);
                else if (i == 4) sysmon_open();
                else if (i == 5) browser_init();
            }
            return;
        }
        lx += ico_w + 2;
    }
}

static void handle_window_btn_click(int mx) {
    int lx = 90;
    for (int i = 0; i < ico_count; i++) lx += (24 + 2);
    lx += 8;
    for (int i = 0; i < window_get_count(); i++) {
        int wid = window_get_id(i);
        window_t *w = window_get(wid);
        if (!w || w->minimized) continue;
        int wb_w = (strlen(w->title) * 8 + 16);
        if (wb_w > 150) wb_w = 150;
        if (wb_w < 60) wb_w = 60;
        if (mx >= lx && mx < lx + wb_w) {
            if (w->minimized) {
                window_toggle_minimize(wid);
            } else {
                if (w->has_focus && !w->minimized) {
                    window_toggle_minimize(wid);
                } else {
                    window_focus(wid);
                }
            }
            return;
        }
        lx += wb_w + 2;
    }
}

static void handle_context_menu_action(int mx, int my) {
    if (!context_menu_open) return;
    int cmx = context_menu_x, cmy = context_menu_y;
    int mw = 160, mh = 140;
    if (mx >= cmx && mx < cmx + mw && my >= cmy && my < cmy + mh) {
        int item = (my - cmy - 4) / 26;
        if (item < 0 || item > 4) return;
        switch (item) {
            case 0: launch_terminal(); break;
            case 1: fileman_open("/home/user"); break;
            case 2: editor_open(0); break;
            case 3: sysmon_open(); break;
            case 4:
                for (int i = 0; i < window_get_count(); i++)
                    window_close(window_get_id(i));
                break;
        }
    }
    context_menu_open = 0;
}

void desktop_init(void) {
    fb_buf = display_get_buffer();
    fb_w = display_get_width();
    fb_h = display_get_height();

    mouse_init();
    window_init();
    shell_init();
    vfs_init();

    fbterm_init(display_get_buffer(), 680, 400);

    for (int i = 0; i < 6; i++) active_launchers[i] = 0;

    launch_terminal();
    browser_init();
}

void desktop_run(void) {
    int last_sec = -1;
    int redraw = 1;

    for (;;) {
        mouse_poll();
        mouse_get_state(&mouse_x, &mouse_y, &mouse_buttons);

        if (mouse_buttons && !prev_mouse_buttons) {
            int handled = window_handle_click(mouse_x, mouse_y, mouse_buttons);
            if (!handled) {
                if (mouse_y < TOPBAR_H) {
                    handle_launcher_click(mouse_x);
                    if (mouse_x >= fb_w - 26 && mouse_x < fb_w - 4) {
                        kprintf("[desktop] power button clicked\n");
                        outw(0xB004, 0x2000);
                        outw(0x604, 0x2000);
                    }
                    handle_window_btn_click(mouse_x);
                } else {
                    int icon = desktop_icon_at(mouse_x, mouse_y);
                    if (icon >= 0) {
                        if (icon == 1) launch_terminal();
                        else {
                            if (icon <= 2) fileman_open(ico_paths[icon]);
                            else if (icon == 3) editor_open(0);
                            else if (icon == 4) sysmon_open();
                            else if (icon == 5) browser_init();
                        }
                    } else {
                        context_menu_open = 1;
                        context_menu_x = mouse_x;
                        context_menu_y = mouse_y;
                    }
                }
            } else {
                context_menu_open = 0;
            }
            handle_context_menu_action(mouse_x, mouse_y);
            redraw = 1;
        }
        prev_mouse_buttons = mouse_buttons;

        window_t *drag_win = window_get_dragging();
        if (drag_win && mouse_buttons) {
            int nx = mouse_x - drag_win->drag_off_x;
            int ny = mouse_y - drag_win->drag_off_y;
            if (nx < 0) nx = 0; if (ny < TOPBAR_H) ny = TOPBAR_H;
            if (nx + drag_win->w > fb_w) nx = fb_w - drag_win->w;
            if (ny + drag_win->h > fb_h) ny = fb_h - drag_win->h;
            drag_win->x = nx;
            drag_win->y = ny;
            redraw = 1;
        } else if (drag_win) {
            drag_win->drag_mode = 0;
            redraw = 1;
        }

        if (drag_win == 0) {
            for (int i = 0; i < window_get_count(); i++) {
                window_t *w = window_get(window_get_id(i));
                if (!w || !w->resize_mode || !mouse_buttons) {
                    if (w) w->resize_mode = 0;
                    continue;
                }
                int dw = mouse_x - (w->x + w->w);
                int dh = mouse_y - (w->y + w->h);
                int new_w = w->w + dw; if (new_w < 200) new_w = 200;
                int new_h = w->h + dh; if (new_h < 100) new_h = 100;
                w->w = new_w; w->h = new_h;
                if (w->id == shell_win_id) {
                    fbterm_init(display_get_buffer(), w->w - 20, w->h - w->titlebar_h - 4);
                }
                redraw = 1;
            }
        }

        int c = keyboard_getc();
        if (c > 0) {
            if (c == 0x1B) context_menu_open = 0;
            if (keyboard_is_ctrl() && c >= 'a' && c <= 'z') {
                switch (c) {
                    case 't': launch_terminal(); break;
                    case 'f': fileman_open("/home/user"); break;
                    case 'e': editor_open(0); break;
                    case 'm': sysmon_open(); break;
                    case 'b': browser_init(); break;
                    default: window_handle_key((char)c); break;
                }
            } else {
                window_handle_key((char)c);
            }
            redraw = 1;
        }

        if (virtio_present) {
            virtio_poll_all();
            tcp_tick();
            browser_tick();
        }

        uint64_t ms = timer_get_ms();
        int cur_sec = (ms / 1000);
        if (cur_sec != last_sec) { last_sec = cur_sec; redraw = 1; }

        fileman_tick();
        sysmon_tick();
        editor_tick();

        if (redraw) {
            draw_background();
            draw_desktop_icons();
            window_render_all();
            draw_topbar();
            draw_context_menu();
            draw_mouse();
            display_present();
            redraw = 0;
        }

        asm("hlt");
    }
}
