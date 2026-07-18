#include "layout.h"
#include "kernel.h"
#include "heap.h"

static void add_box(layout_box_t **boxes, int *count, int *cap, layout_box_t b) {
    if (*count >= *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *boxes = (layout_box_t *)krealloc(*boxes, sizeof(layout_box_t) * *cap);
    }
    (*boxes)[(*count)++] = b;
}

static void layout_node(html_node_t *node, computed_style_t *parent_style,
                        int *cx, int *cy, int vp_w, int margin,
                        layout_box_t **boxes, int *count, int *cap,
                        const char *link_url) {
    if (!node) return;

    const char *my_link = link_url;
    if (node->type == NODE_ELEMENT && strcmp(node->tag, "a") == 0) {
        for (int i = 0; i < node->attr_count; i++)
            if (strcmp(node->attrs[i].name, "href") == 0) my_link = node->attrs[i].value;
    }

    computed_style_t s = css_compute(node, parent_style);
    if (node->type == NODE_ELEMENT && strcmp(node->tag, "body") == 0) {
        *cx = s.margin_left;
        *cy = s.margin_top;
    }

    if (node->type == NODE_ELEMENT && strcmp(node->tag, "br") == 0) {
        *cx = s.margin_left;
        *cy += 16;
        return;
    }

    if (node->type == NODE_ELEMENT && strcmp(node->tag, "img") == 0) {
        int iw = 16, ih = 16;
        const char *alt = 0;
        for (int i = 0; i < node->attr_count; i++) {
            if (strcmp(node->attrs[i].name, "width") == 0) iw = 0;
            if (strcmp(node->attrs[i].name, "height") == 0) ih = 0;
            if (strcmp(node->attrs[i].name, "alt") == 0) alt = node->attrs[i].value;
        }
        if (*cx + iw + s.margin_right > vp_w) { *cx = s.margin_left; *cy += ih + 4; }
        layout_box_t b;
        memset(&b, 0, sizeof(b));
        b.x = *cx; b.y = *cy; b.w = iw; b.h = ih;
        b.node = node; b.style = s;
        b.is_image = 1;
        if (alt) { b.img_alt = (char *)kmalloc(strlen(alt) + 1); strcpy(b.img_alt, alt); }
        b.img_w = iw; b.img_h = ih;
        add_box(boxes, count, cap, b);
        *cx += iw + 4;
        return;
    }

    if (s.display_block) {
        if (*cx != s.margin_left || *cy > 0) {
            *cx = s.margin_left;
            *cy += 16;
        }
        *cy += s.margin_top;
    }

    if (node->type == NODE_TEXT && node->text) {
        const char *t = node->text;
        while (*t) {
            if (*cx + 8 > vp_w - s.margin_right) {
                *cx = s.margin_left;
                *cy += 16;
            }
            int word_start = *cx;
            const char *word = t;
            while (*t && *t != ' ' && *t != '\n') t++;
            int wlen = (int)(t - word);
            if (wlen > 0) {
                int word_px = wlen * 8;
                if (word_start + word_px > vp_w - s.margin_right) {
                    *cx = s.margin_left;
                    *cy += 16;
                    word_start = *cx;
                }
                for (int i = 0; i < wlen; i++) {
                    layout_box_t b;
                    memset(&b, 0, sizeof(b));
                    b.x = *cx; b.y = *cy; b.w = 8; b.h = 16;
                    b.node = node; b.style = s;
                    b.text = (char *)kmalloc(2); b.text[0] = word[i]; b.text[1] = 0;
                    if (my_link) {
                        b.is_link = 1;
                        b.link_url = (char *)kmalloc(strlen(my_link) + 1);
                        strcpy(b.link_url, my_link);
                    }
                    add_box(boxes, count, cap, b);
                    *cx += 8;
                }
            }
            if (*t == ' ') { *cx += 4; t++; }
            else if (*t == '\n') { *cx = s.margin_left; *cy += 16; t++; }
        }
    }

    for (int i = 0; i < node->child_count; i++)
        layout_node(node->children[i], &s, cx, cy, vp_w, margin, boxes, count, cap, my_link);

    if (s.display_block) {
        *cy += s.margin_bottom;
        *cx = s.margin_left;
    }
}

int layout_document(html_node_t *root, int viewport_w, layout_box_t **boxes, int *box_count) {
    *boxes = 0;
    *box_count = 0;
    int cap = 0;
    int cx = 8, cy = 4;
    computed_style_t def = css_default();
    def.margin_left = 8; def.margin_right = 8;

    html_node_t *body = 0;
    if (root && root->child_count > 0) body = root->children[0];

    layout_node(body ? body : root, &def, &cx, &cy, viewport_w, 8, boxes, box_count, &cap, 0);
    return *box_count;
}

void layout_free_boxes(layout_box_t *boxes, int count) {
    if (!boxes) return;
    for (int i = 0; i < count; i++) {
        if (boxes[i].text) kfree(boxes[i].text);
        if (boxes[i].link_url) kfree(boxes[i].link_url);
        if (boxes[i].img_alt) kfree(boxes[i].img_alt);
    }
    kfree(boxes);
}
