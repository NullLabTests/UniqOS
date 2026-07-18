#pragma once

#include <stdint.h>

void display_init(void);
int display_is_fb(void);
void display_put_pixel(int x, int y, uint32_t color);
void display_fill_rect(int x, int y, int w, int h, uint32_t color);
void display_present(void);
int display_get_width(void);
int display_get_height(void);
uint32_t *display_get_buffer(void);
void display_put_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void display_put_char_styled(int x, int y, char c, uint32_t fg, uint32_t bg, int bold, int italic);
