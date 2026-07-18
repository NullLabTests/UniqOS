#pragma once

#include <stdint.h>

void keyboard_init(void);
int keyboard_getc(void);
void keyboard_process_scancode(uint8_t scancode);
int keyboard_is_ctrl(void);
int keyboard_scancode(void);
