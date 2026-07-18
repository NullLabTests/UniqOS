#pragma once

#include <stdint.h>
#include "html_parse.h"

typedef struct {
    int bold;
    int italic;
    int underline;
    uint32_t color;
    uint32_t bg_color;
    uint32_t link_color;
    int margin_top;
    int margin_bottom;
    int margin_left;
    int margin_right;
    int display_block;
} computed_style_t;

computed_style_t css_compute(html_node_t *node, computed_style_t *parent);

computed_style_t css_default(void);
