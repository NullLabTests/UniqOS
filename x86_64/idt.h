#pragma once

#include <stdint.h>

typedef struct {
    uint16_t base_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

void idt_init(void);
void isr_handler(uint64_t *frame);
void irq_handler(uint64_t *frame);
void irq_register_handler(int irq, void (*handler)(uint64_t));
