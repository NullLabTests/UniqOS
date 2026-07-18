#include "html_parse.h"
#include "kernel.h"
#include "heap.h"

static const char *skip_ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static int is_tag_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
}

static int is_self_closing(const char *tag) {
    const char *tags[] = {"br","img","hr","input","meta","link","area","base","col","embed","source","track","wbr",0};
    for (int i = 0; tags[i]; i++)
        if (strcmp(tag, tags[i]) == 0) return 1;
    return 0;
}

static html_node_t *make_node(node_type_t type, const char *tag, const char *text) {
    html_node_t *n = (html_node_t *)kmalloc(sizeof(html_node_t));
    memset(n, 0, sizeof(html_node_t));
    n->type = type;
    if (tag) { n->tag = (char *)kmalloc(strlen(tag) + 1); strcpy(n->tag, tag); }
    if (text) { n->text = (char *)kmalloc(strlen(text) + 1); strcpy(n->text, text); }
    return n;
}

static void add_child(html_node_t *parent, html_node_t *child) {
    child->parent = parent;
    html_node_t **newc = (html_node_t **)kmalloc(sizeof(html_node_t *) * (parent->child_count + 1));
    for (int i = 0; i < parent->child_count; i++) newc[i] = parent->children[i];
    newc[parent->child_count++] = child;
    if (parent->children) kfree(parent->children);
    parent->children = newc;
}

static void add_attr(html_node_t *node, const char *name, const char *value) {
    html_attr_t *newa = (html_attr_t *)kmalloc(sizeof(html_attr_t) * (node->attr_count + 1));
    for (int i = 0; i < node->attr_count; i++) newa[i] = node->attrs[i];
    newa[node->attr_count].name = (char *)kmalloc(strlen(name) + 1); strcpy(newa[node->attr_count].name, name);
    newa[node->attr_count].value = (char *)kmalloc(strlen(value) + 1); strcpy(newa[node->attr_count].value, value);
    node->attr_count++;
    if (node->attrs) kfree(node->attrs);
    node->attrs = newa;
}

static const char *parse_tag_name(const char *p, const char *end, char *buf, int bufsz) {
    int i = 0;
    while (p < end && is_tag_char(*p) && i < bufsz - 1) buf[i++] = *p++;
    buf[i] = 0;
    return p;
}

static const char *parse_attr_value(const char *p, const char *end, char *buf, int bufsz) {
    p = skip_ws(p, end);
    if (p >= end || *p != '=') return p;
    p++; p = skip_ws(p, end);
    if (p >= end) return p;
    char quote = *p;
    if (quote != '"' && quote != '\'') return p;
    p++;
    int i = 0;
    while (p < end && *p != quote && i < bufsz - 1) {
        if (*p == '&' && p + 4 < end && p[1] == 'a' && p[2] == 'm' && p[3] == 'p' && p[4] == ';') {
            buf[i++] = '&'; p += 5;
        } else if (*p == '&' && p + 3 < end && p[1] == 'l' && p[2] == 't' && p[3] == ';') {
            buf[i++] = '<'; p += 4;
        } else if (*p == '&' && p + 3 < end && p[1] == 'g' && p[2] == 't' && p[3] == ';') {
            buf[i++] = '>'; p += 4;
        } else {
            buf[i++] = *p++;
        }
    }
    buf[i] = 0;
    if (p < end) p++;
    return p;
}

static const char *parse_node(const char *p, const char *end, html_node_t **out);

static const char *parse_element(const char *p, const char *end, html_node_t **out) {
    p++; if (p >= end) return p;
    char tagname[64];
    p = parse_tag_name(p, end, tagname, sizeof(tagname));
    if (tagname[0] == 0) return p;

    html_node_t *node = make_node(NODE_ELEMENT, tagname, 0);

    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end) break;
        if (*p == '>' || (*p == '/' && p + 1 < end && p[1] == '>')) break;
        if (*p == '<') break;

        char attrname[64], attrval[256];
        int i = 0;
        while (p < end && is_tag_char(*p) && i < 63) attrname[i++] = *p++;
        attrname[i] = 0;
        if (attrname[0] == 0) break;

        p = parse_attr_value(p, end, attrval, sizeof(attrval));
        add_attr(node, attrname, attrval);
    }

    int self_close = 0;
    if (p < end && *p == '/') { self_close = 1; p++; }
    if (p < end) p++;

    if (self_close || is_self_closing(tagname)) {
        *out = node;
        return p;
    }

    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end) break;
        if (*p == '<' && p + 1 < end && p[1] == '/') {
            p += 2;
            char endtag[64];
            p = parse_tag_name(p, end, endtag, sizeof(endtag));
            if (endtag[0] && strcmp(endtag, tagname) == 0) {
                while (p < end && *p != '>') p++;
                if (p < end) p++;
            }
            break;
        }
        html_node_t *child = 0;
        p = parse_node(p, end, &child);
        if (child) add_child(node, child);
    }

    *out = node;
    return p;
}

static const char *parse_text(const char *p, const char *end, html_node_t **out) {
    char buf[4096];
    int i = 0;
    while (p < end && *p != '<' && i < 4095) {
        if (*p == '&' && p + 4 < end && p[1] == 'a' && p[2] == 'm' && p[3] == 'p' && p[4] == ';') {
            buf[i++] = '&'; p += 5;
        } else if (*p == '&' && p + 3 < end && p[1] == 'l' && p[2] == 't' && p[3] == ';') {
            buf[i++] = '<'; p += 4;
        } else if (*p == '&' && p + 3 < end && p[1] == 'g' && p[2] == 't' && p[3] == ';') {
            buf[i++] = '>'; p += 4;
        } else if (*p == '&' && p + 5 < end && p[1] == 'n' && p[2] == 'b' && p[3] == 's' && p[4] == 'p' && p[5] == ';') {
            buf[i++] = ' '; p += 6;
        } else {
            buf[i++] = *p++;
        }
    }
    buf[i] = 0;
    if (i == 0) { *out = 0; return p; }
    int start = 0;
    while (start < i && buf[start] == ' ') start++;
    if (start > 0) { start = 0; }
    if (start < i) {
        *out = make_node(NODE_TEXT, 0, buf + start);
    } else {
        *out = 0;
    }
    return p;
}

static const char *parse_node(const char *p, const char *end, html_node_t **out) {
    if (p >= end) { *out = 0; return p; }
    p = skip_ws(p, end);
    if (p >= end) { *out = 0; return p; }
    if (*p == '<') {
        if (p + 1 < end && p[1] == '/') {
            *out = 0;
            return p;
        }
        return parse_element(p, end, out);
    }
    return parse_text(p, end, out);
}

html_node_t *html_parse(const char *html, int len) {
    if (!html || len <= 0) return 0;
    const char *end = html + len;
    html_node_t *root = make_node(NODE_ELEMENT, "html", 0);
    html_node_t *body = make_node(NODE_ELEMENT, "body", 0);
    add_child(root, body);

    const char *p = html;
    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end) break;
        if (*p == '<' && p + 1 < end && p[1] == '!') {
            while (p < end && *p != '>') p++;
            if (p < end) p++;
            continue;
        }
        html_node_t *child = 0;
        p = parse_node(p, end, &child);
        if (child) add_child(body, child);
    }
    return root;
}

void html_free(html_node_t *node) {
    if (!node) return;
    if (node->tag) kfree(node->tag);
    if (node->text) kfree(node->text);
    for (int i = 0; i < node->attr_count; i++) {
        if (node->attrs[i].name) kfree(node->attrs[i].name);
        if (node->attrs[i].value) kfree(node->attrs[i].value);
    }
    if (node->attrs) kfree(node->attrs);
    for (int i = 0; i < node->child_count; i++) html_free(node->children[i]);
    if (node->children) kfree(node->children);
    kfree(node);
}
