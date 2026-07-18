#include "editor.h"
#include "window.h"
#include "display.h"
#include "font.h"
#include "vfs.h"
#include "kernel.h"
#include "heap.h"

#define EDIT_LINES 512
#define EDIT_COLS 256
#define GUTTER_W 44

typedef struct {
    char path[256];
    char lines[EDIT_LINES][EDIT_COLS];
    int line_count;
    int cx, cy;
    int scroll_x, scroll_y;
    int dirty;
    int modified;
} editor_t;

static editor_t *ed_data = 0;

// Syntax highlighting colors
#define C_KEYWORD  0x00FF9966
#define C_TYPE     0x0066CCFF
#define C_STRING   0x0099CC66
#define C_COMMENT  0x00668888
#define C_NUMBER   0x00CC88FF
#define C_PREPROC  0x00FF88AA
#define C_OPERATOR 0x00DDDDDD
#define C_DEFAULT  0x00CCCCCC
#define C_GUTTER_BG 0x00202040
#define C_GUTTER_FG 0x00667788
#define C_CURSOR_BG 0x00335577

static const char *c_keywords[] = {
    "auto","break","case","const","continue","default","do","else","enum",
    "extern","for","goto","if","register","return","signed","static",
    "struct","switch","typedef","union","unsigned","volatile","while",
    "sizeof","inline","restrict", 0
};

static const char *c_types[] = {
    "int","char","void","long","short","float","double","size_t",
    "int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t",
    "uint32_t","uint64_t","uintptr_t","ssize_t","off_t","bool",
    "uint8_t","uint16_t","uint32_t","uint64_t","ip_t","mac_t", 0
};

static const char *asm_keywords[] = {
    "mov","add","sub","mul","div","push","pop","call","ret","jmp",
    "je","jne","jg","jl","jge","jle","cmp","test","and","or","xor",
    "not","shl","shr","inc","dec","int","syscall","lea","nop",
    "global","extern","section","align","bits","org","dw","db","dd",
    "resb","resw","resd","times","equ","%define","%macro","%endmacro", 0
};

static const int is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static const int is_digit(char c) { return c >= '0' && c <= '9'; }
static const int is_alnum(char c) { return is_letter(c) || is_digit(c); }

static int match_list(const char *word, const char **list) {
    for (int i = 0; list[i]; i++) {
        const char *a = word, *b = list[i];
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*a && !*b) return 1;
        if (!*b && !is_alnum(*a)) return 1;
    }
    return 0;
}

static void syntax_color(const char *line, uint32_t *colors, int max_cols) {
    int len = strlen(line);
    if (len > max_cols) len = max_cols;
    for (int i = 0; i < len; i++) colors[i] = C_DEFAULT;

    int in_string = 0, in_char = 0, in_comment = 0, in_preproc = 0;
    int line_is_preproc = (line[0] == '#');

    for (int i = 0; i < len; i++) {
        char c = line[i];

        if (in_comment) {
            colors[i] = C_COMMENT;
            if (c == '*' && i + 1 < len && line[i+1] == '/') {
                colors[i+1] = C_COMMENT;
                in_comment = 0;
                i++;
            }
            continue;
        }

        if (in_string) {
            colors[i] = C_STRING;
            if (c == '\\' && i + 1 < len) { colors[i+1] = C_STRING; i++; }
            else if (c == '"') in_string = 0;
            continue;
        }

        if (in_char) {
            colors[i] = C_STRING;
            if (c == '\\' && i + 1 < len) { colors[i+1] = C_STRING; i++; }
            else if (c == '\'') in_char = 0;
            continue;
        }

        if (c == '/' && i + 1 < len) {
            if (line[i+1] == '/') {
                while (i < len) colors[i++] = C_COMMENT;
                return;
            }
            if (line[i+1] == '*') {
                colors[i] = C_COMMENT;
                colors[i+1] = C_COMMENT;
                in_comment = 1;
                i++;
                continue;
            }
        }

        if (c == '"') { colors[i] = C_STRING; in_string = 1; continue; }
        if (c == '\'') { colors[i] = C_STRING; in_char = 1; continue; }

        if (line_is_preproc && i == 0) {
            int j = i;
            while (j < len && line[j] != ' ' && line[j] != '\t') colors[j++] = C_PREPROC;
            i = j - 1;
            continue;
        }

        if (is_digit(c)) {
            colors[i] = C_NUMBER;
            if (c == '0' && i + 1 < len && (line[i+1] == 'x' || line[i+1] == 'X')) {
                colors[i+1] = C_NUMBER; i++;
                while (i + 1 < len && ((line[i+1] >= '0' && line[i+1] <= '9') ||
                      (line[i+1] >= 'a' && line[i+1] <= 'f') ||
                      (line[i+1] >= 'A' && line[i+1] <= 'F'))) { colors[i+1] = C_NUMBER; i++; }
            } else {
                while (i + 1 < len && is_digit(line[i+1])) { colors[i+1] = C_NUMBER; i++; }
            }
            continue;
        }

        if (is_letter(c)) {
            int start = i;
            while (i + 1 < len && is_alnum(line[i+1])) i++;
            char word[64]; int wi = 0;
            for (int j = start; j <= i && wi < 60; j++) word[wi++] = line[j];
            word[wi] = 0;

            if (match_list(word, c_keywords) || match_list(word, asm_keywords))
                for (int j = start; j <= i; j++) colors[j] = C_KEYWORD;
            else if (match_list(word, c_types))
                for (int j = start; j <= i; j++) colors[j] = C_TYPE;
            continue;
        }
    }
}

void editor_open(const char *path) {
    if (!ed_data) {
        ed_data = (editor_t *)kmalloc(sizeof(editor_t));
        ed_data->path[0] = 0;
        ed_data->line_count = 1;
        ed_data->lines[0][0] = 0;
        ed_data->cx = 0; ed_data->cy = 0;
        ed_data->scroll_x = 0; ed_data->scroll_y = 0;
        ed_data->modified = 0;
    }
    editor_t *ed = ed_data;
    if (path) {
        int i = 0; for (; *path && i < 255; i++) ed->path[i] = *path++;
        ed->path[i] = 0;
        char buf[16384];
        int r = vfs_read(ed->path, buf, 16383);
        if (r > 0) {
            buf[r] = 0;
            ed->line_count = 0;
            int lp = 0;
            ed->lines[0][0] = 0;
            for (int j = 0; j < r && ed->line_count < EDIT_LINES; j++) {
                if (buf[j] == '\n') {
                    ed->lines[ed->line_count][lp] = 0;
                    ed->line_count++;
                    lp = 0;
                } else if (lp < EDIT_COLS - 1) {
                    ed->lines[ed->line_count][lp++] = buf[j];
                }
            }
            ed->lines[ed->line_count][lp] = 0;
            if (buf[r-1] != '\n' || ed->line_count == 0) ed->line_count++;
            ed->modified = 0;
        }
    }
    ed->cx = 0; ed->cy = 0;
    ed->scroll_x = 0; ed->scroll_y = 0;
    ed->dirty = 1;

    int wid = editor_window_id();
    if (wid < 0)
        wid = window_create(250, 70, 650, 400, "Text Editor");
    window_set_title(wid, path ? path : "untitled");

    window_t *win = window_get(wid);
    if (win) win->user_data = ed;
    window_focus(wid);
}

int editor_window_id(void) {
    if (!ed_data) return -1;
    window_t *win = window_get_by_userdata(ed_data);
    return win ? win->id : -1;
}

void editor_tick(void) {
    window_t *win = 0;
    if (ed_data) win = window_get_by_userdata(ed_data);
    if (!win || !ed_data) return;
    editor_t *ed = ed_data;

    ed->dirty = 1;

    int x = win->x, y = win->y;
    int bw = win->w, bh = win->h;
    int text_x = x + GUTTER_W + 4;
    int text_y = y + win->titlebar_h + 2;
    int text_w = bw - GUTTER_W - 8;
    int text_h = bh - win->titlebar_h - 22;
    int max_cols = text_w / 8;
    int max_rows = text_h / 16;

    // Gutter
    for (int row = 0; row < max_rows && row + ed->scroll_y < ed->line_count; row++) {
        int ln = row + ed->scroll_y + 1;
        uint32_t gbg = (ln == ed->cy + 1) ? C_CURSOR_BG : C_GUTTER_BG;
        display_fill_rect(x + 1, text_y + row * 16, GUTTER_W, 16, gbg);
        unsigned v = ln; char tn[12]; int tp = 0;
        do { tn[tp++] = '0' + (v % 10); v /= 10; } while (v);
        int gn = GUTTER_W - 10;
        for (int j = tp - 1; j >= 0; j--) {
            display_put_char(x + gn, text_y + row * 16, tn[j], C_GUTTER_FG, gbg);
            gn -= 8;
        }
    }

    // Color buffer for syntax highlighting
    uint32_t line_colors[EDIT_COLS];

    // Text area
    for (int row = 0; row < max_rows && row + ed->scroll_y < ed->line_count; row++) {
        int l = row + ed->scroll_y;
        int is_cursor_line = (l == ed->cy);
        uint32_t bg = is_cursor_line ? 0x00224466 : 0x001C1C3A;
        display_fill_rect(text_x, text_y + row * 16, text_w, 16, bg);

        const char *line = ed->lines[l];
        int linelen = strlen(line);
        syntax_color(line, line_colors, EDIT_COLS);

        for (int col = 0; col < max_cols && col + ed->scroll_x < linelen; col++) {
            int sci = col + ed->scroll_x;
            char c = line[sci];
            uint32_t fg = line_colors[sci];
            if (fg == C_DEFAULT) fg = is_cursor_line ? 0x00FFFFFF : 0x00CCCCCC;

            if (ed->cx == sci && ed->cy == l && (timer_get_ms() / 500) % 2)
                display_fill_rect(text_x + col * 8, text_y + row * 16, 8, 16, 0x00FFFFFF);
            display_put_char(text_x + col * 8, text_y + row * 16, c, fg, bg);
        }
        if (ed->cx >= linelen && ed->cy == l && (timer_get_ms() / 500) % 2) {
            int ccol = linelen - ed->scroll_x;
            if (ccol >= 0 && ccol < max_cols)
                display_fill_rect(text_x + ccol * 8, text_y + row * 16, 8, 16, 0x00FFFFFF);
        }
    }

    // Status bar
    int st_y = y + bh - 18;
    display_fill_rect(x + 1, st_y, bw - 2, 17, 0x0028284A);
    char st[64]; int sp = 0;
    const char *fn = ed->path[0] ? ed->path : "untitled";
    while (*fn) st[sp++] = *fn++;
    st[sp++] = ' '; st[sp++] = '|'; st[sp++] = ' ';
    unsigned lc = ed->cy + 1; char tc[12]; int tp = 0;
    do { tc[tp++] = '0' + (lc % 10); lc /= 10; } while (lc);
    while (tp > 0) st[sp++] = tc[--tp];
    st[sp++] = ':';
    unsigned cc = ed->cx + 1; tp = 0;
    do { tc[tp++] = '0' + (cc % 10); cc /= 10; } while (cc);
    while (tp > 0) st[sp++] = tc[--tp];
    if (ed->modified) { st[sp++] = ' '; st[sp++] = '*'; }
    st[sp] = 0;
    for (int i = 0; st[i]; i++)
        display_put_char(x + 6 + i * 8, st_y + 1, st[i], 0x009999BB, 0x0028284A);

    ed->dirty = 0;
}
