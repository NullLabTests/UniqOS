#include "render.h"
#include "display.h"
#include "font.h"
#include "kernel.h"
#include "heap.h"

void render_boxes(layout_box_t *boxes, int count, int win_x, int win_y, int win_w, int win_h) {
    (void)win_w; (void)win_h;
    for (int i = 0; i < count; i++) {
        layout_box_t *b = &boxes[i];
        int rx = win_x + b->x;
        int ry = win_y + b->y;

        if (b->is_image) {
            display_fill_rect(rx, ry, b->w, b->h, 0x00335566);
            if (b->img_alt) {
                for (int j = 0; b->img_alt[j]; j++)
                    display_put_char(rx + 2 + j * 8, ry + 2, b->img_alt[j], 0x00FFFFFF, 0x00335566);
            }
            continue;
        }

        if (b->text) {
            char c = b->text[0];
            if (c < 32 || c > 126) continue;
            computed_style_t *s = &b->style;

            if (b->is_link) {
                display_put_char_styled(rx, ry, c, s->link_color, s->bg_color, s->bold, s->italic);
                uint32_t uc = s->link_color;
                int ul_y = ry + 14;
                if (ul_y >= 0 && ul_y < 768) {
                    uint32_t *fb = display_get_buffer();
                    int fb_w = display_get_width();
                    for (int col = 0; col < 8 && rx + col < fb_w; col++)
                        if (rx + col >= 0) fb[ul_y * fb_w + (rx + col)] = uc;
                }
            } else {
                display_put_char_styled(rx, ry, c, s->color, s->bg_color, s->bold, s->italic);
                if (s->underline) {
                    uint32_t *fb = display_get_buffer();
                    int fb_w = display_get_width();
                    int ul_y = ry + 14;
                    if (ul_y >= 0 && ul_y < 768) {
                        for (int col = 0; col < 8 && rx + col < fb_w; col++)
                            if (rx + col >= 0) fb[ul_y * fb_w + (rx + col)] = s->color;
                    }
                }
            }
        }
    }
}
