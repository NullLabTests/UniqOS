#pragma once

#include <stdint.h>

void vmm_init(void);
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t vmm_get_phys(uint64_t virt);
void vmm_switch_page_table(uint64_t *pml4);
