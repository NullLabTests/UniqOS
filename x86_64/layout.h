#pragma once

#include <stdint.h>
#include "html_parse.h"
#include "css_minimal.h"

typedef struct {
    int x, y, w, h;
    html_node_t *node;
    computed_style_t style;
    char *text;
    int is_link;
    char *link_url;
    int is_image;
    char *img_alt;
    int img_w, img_h;
} layout_box_t;

int layout_document(html_node_t *root, int viewport_w, layout_box_t **boxes, int *box_count);
void layout_free_boxes(layout_box_t *boxes, int count);
