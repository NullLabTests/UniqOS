#pragma once

#include <stdint.h>

void terminal_init(uint32_t *fb, int w, int h, int pitch, void (*flush)(void));
void terminal_putchar(char c);
void terminal_write(const char *s);
void terminal_printf(const char *fmt, ...);
void terminal_clear(void);
void terminal_set_fg(uint32_t color);
void terminal_set_bg(uint32_t color);
void terminal_scroll(void);
void terminal_set_cursor(int row, int col);
