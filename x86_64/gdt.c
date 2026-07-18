#include "gdt.h"
#include "kernel.h"

static gdt_entry_t gdt_entries[6];
static gdt_ptr_t gdt_ptr;

static void gdt_set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt_entries[i].limit_low = limit & 0xFFFF;
    gdt_entries[i].base_low = base & 0xFFFF;
    gdt_entries[i].base_mid = (base >> 16) & 0xFF;
    gdt_entries[i].access = access;
    gdt_entries[i].flags_limit_high = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    gdt_entries[i].base_high = (base >> 24) & 0xFF;
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt_entries) - 1;
    gdt_ptr.base = (uint64_t)&gdt_entries;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xA0);
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xA0);
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xA0);
    gdt_set_entry(5, 0, 0, 0, 0);

    asm volatile("lgdt %0" : : "m"(gdt_ptr));
    asm volatile("push $0x08; lea 1f(%%rip), %%rax; push %%rax; lretq; 1:" ::: "rax");
    asm volatile("mov $0x10, %%ax; mov %%ax, %%ds; mov %%ax, %%es; mov %%ax, %%ss; mov %%ax, %%fs; mov %%ax, %%gs" ::: "ax");
}
