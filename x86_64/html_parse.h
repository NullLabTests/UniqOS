#pragma once

#include <stdint.h>

typedef enum { NODE_ELEMENT, NODE_TEXT } node_type_t;

typedef struct html_attr {
    char *name;
    char *value;
} html_attr_t;

typedef struct html_node {
    node_type_t type;
    char *tag;
    char *text;
    html_attr_t *attrs;
    int attr_count;
    struct html_node **children;
    int child_count;
    struct html_node *parent;
} html_node_t;

html_node_t *html_parse(const char *html, int len);
void html_free(html_node_t *root);
