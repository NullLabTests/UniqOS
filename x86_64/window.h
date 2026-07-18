#pragma once
#include <stdint.h>

#define MAX_WINDOWS 32
#define TITLEBAR_H 24
#define WINDOW_BORDER 2
#define BTN_W 14
#define BTN_H 14

typedef struct window {
    int id;
    int x, y, w, h;
    int restore_x, restore_y, restore_w, restore_h;
    int titlebar_h;
    char title[64];
    int visible;
    int minimized;
    int maximized;
    int has_focus;
    int drag_mode;
    int drag_off_x, drag_off_y;
    int resize_mode;
    int resize_edge;
    uint32_t bg_color;
    uint32_t title_color;
    void *user_data;
    void (*draw)(struct window *w);
    void (*on_key)(struct window *w, char c);
    void (*on_close)(struct window *w);
} window_t;

void window_init(void);
int window_create(int x, int y, int w, int h, const char *title);
void window_close(int id);
void window_move(int id, int x, int y);
void window_resize(int id, int w, int h);
void window_focus(int id);
void window_set_draw(int id, void (*draw)(window_t *));
void window_set_onkey(int id, void (*onkey)(window_t *, char));
void window_set_onclose(int id, void (*onclose)(window_t *));
window_t *window_get(int id);
window_t *window_get_by_userdata(void *data);
void window_render_all(void);
int window_handle_click(int mx, int my, int buttons);
int window_handle_key(char c);
void window_set_title(int id, const char *title);
window_t *window_get_dragging(void);
void window_toggle_minimize(int id);
void window_toggle_maximize(int id);
int window_get_count(void);
int window_get_id(int index);
int window_is_minimized(int id);
