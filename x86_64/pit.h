#pragma once

#include <stdint.h>

void pit_init(uint32_t frequency);
uint64_t pit_get_tick(void);
