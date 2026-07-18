#pragma once

#include <stdint.h>
#include "layout.h"

typedef struct {
    int x, y, w, h;
    char *url;
} link_rect_t;

void link_map_build(layout_box_t *boxes, int count, int win_x, int win_y,
                    link_rect_t **out, int *out_count);
int link_map_click(link_rect_t *map, int count, int mx, int my, char **out_url);
void link_map_free(link_rect_t *map, int count);
