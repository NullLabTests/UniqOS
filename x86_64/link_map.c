#include "link_map.h"
#include "kernel.h"
#include "heap.h"

void link_map_build(layout_box_t *boxes, int count, int win_x, int win_y,
                    link_rect_t **out, int *out_count) {
    *out = 0;
    *out_count = 0;
    int cap = 0;

    for (int i = 0; i < count; i++) {
        if (!boxes[i].is_link || !boxes[i].link_url) continue;

        int found = 0;
        for (int j = 0; j < *out_count; j++) {
            if (strcmp((*out)[j].url, boxes[i].link_url) == 0) {
                int bx = win_x + boxes[i].x;
                int by = win_y + boxes[i].y;
                if (bx < (*out)[j].x) (*out)[j].x = bx;
                if (by < (*out)[j].y) (*out)[j].y = by;
                int r = (*out)[j].x + (*out)[j].w;
                if (bx + boxes[i].w > r) (*out)[j].w = bx + boxes[i].w - (*out)[j].x;
                int b = (*out)[j].y + (*out)[j].h;
                if (by + boxes[i].h > b) (*out)[j].h = by + boxes[i].h - (*out)[j].y;
                found = 1;
                break;
            }
        }
        if (found) continue;

        if (*out_count >= cap) {
            cap = cap ? cap * 2 : 16;
            *out = (link_rect_t *)krealloc(*out, sizeof(link_rect_t) * cap);
        }
        link_rect_t *r = &(*out)[*out_count];
        r->x = win_x + boxes[i].x;
        r->y = win_y + boxes[i].y;
        r->w = boxes[i].w;
        r->h = boxes[i].h;
        r->url = (char *)kmalloc(strlen(boxes[i].link_url) + 1);
        strcpy(r->url, boxes[i].link_url);
        (*out_count)++;
    }
}

int link_map_click(link_rect_t *map, int count, int mx, int my, char **out_url) {
    for (int i = 0; i < count; i++) {
        if (mx >= map[i].x && mx < map[i].x + map[i].w &&
            my >= map[i].y && my < map[i].y + map[i].h) {
            *out_url = map[i].url;
            return 1;
        }
    }
    return 0;
}

void link_map_free(link_rect_t *map, int count) {
    if (!map) return;
    for (int i = 0; i < count; i++)
        if (map[i].url) kfree(map[i].url);
    kfree(map);
}
