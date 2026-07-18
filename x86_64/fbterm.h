#pragma once

#include <stdint.h>

void fbterm_init(uint32_t *fb, int w, int h);
void fbterm_putchar(char c);
void fbterm_write(const char *s);
void fbterm_printf(const char *fmt, ...);
void fbterm_clear(void);
void fbterm_set_fg(uint32_t c);
void fbterm_set_bg(uint32_t c);
void fbterm_render(void);
void fbterm_scroll(void);
