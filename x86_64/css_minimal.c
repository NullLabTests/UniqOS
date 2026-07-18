#include "css_minimal.h"
#include "kernel.h"

static int tag_eq(html_node_t *n, const char *s) {
    return n && n->tag && strcmp(n->tag, s) == 0;
}

computed_style_t css_default(void) {
    computed_style_t s;
    s.bold = 0; s.italic = 0; s.underline = 0;
    s.color = 0x00CCCCCC;
    s.bg_color = 0x00101010;
    s.link_color = 0x006688FF;
    s.margin_top = 0; s.margin_bottom = 0;
    s.margin_left = 8; s.margin_right = 8;
    s.display_block = 0;
    return s;
}

computed_style_t css_compute(html_node_t *node, computed_style_t *parent) {
    computed_style_t s = parent ? *parent : css_default();
    if (!node || !node->tag) return s;

    if (tag_eq(node, "a")) {
        s.underline = 1;
        s.color = s.link_color;
    }
    if (tag_eq(node, "b") || tag_eq(node, "strong")) s.bold = 1;
    if (tag_eq(node, "i") || tag_eq(node, "em")) s.italic = 1;
    if (tag_eq(node, "u")) s.underline = 1;
    if (tag_eq(node, "h1") || tag_eq(node, "h2") || tag_eq(node, "h3") ||
        tag_eq(node, "h4") || tag_eq(node, "h5") || tag_eq(node, "h6")) {
        s.bold = 1; s.margin_top = 8; s.margin_bottom = 4; s.display_block = 1;
    }
    if (tag_eq(node, "p")) { s.margin_top = 4; s.margin_bottom = 4; s.display_block = 1; }
    if (tag_eq(node, "div")) { s.display_block = 1; }
    if (tag_eq(node, "li")) { s.margin_left = 24; s.display_block = 1; }
    if (tag_eq(node, "br")) { s.display_block = 1; s.margin_top = 0; s.margin_bottom = 0; }
    if (tag_eq(node, "img")) { s.display_block = 1; }
    if (tag_eq(node, "blockquote")) { s.margin_left = 24; s.margin_top = 4; s.margin_bottom = 4; }

    return s;
}
