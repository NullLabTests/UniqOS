#pragma once

#include <stdint.h>

void mouse_init(void);
void mouse_get_state(int *x, int *y, int *buttons);
void mouse_poll(void);
