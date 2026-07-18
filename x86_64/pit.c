#include "pit.h"
#include "idt.h"
#include "kernel.h"
#include "serial.h"

static volatile uint64_t tick = 0;

void pit_handler(uint64_t frame) {
    (void)frame;
    tick++;
}

void pit_init(uint32_t frequency) {
    irq_register_handler(0, pit_handler);
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

uint64_t pit_get_tick(void) {
    return tick;
}

void timer_sleep(uint64_t ms) {
    uint64_t target = ms / 10;
    uint64_t start = tick;
    while (tick - start < target) {
        asm("pause");
    }
}

uint64_t timer_get_ms(void) {
    return tick * 10;
}
