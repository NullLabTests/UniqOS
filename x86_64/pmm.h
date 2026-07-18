#pragma once

#include <stdint.h>

void pmm_init(uint64_t mem_upper);
void *pmm_alloc_page(void);
void pmm_free_page(void *ptr);
void *pmm_alloc(void);
void pmm_free(void *ptr);
uint64_t pmm_get_free_count(void);
