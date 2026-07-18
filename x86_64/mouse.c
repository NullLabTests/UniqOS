#include "mouse.h"
#include "idt.h"
#include "keyboard.h"
#include "serial.h"

#define MOUSE_IRQ 12
#define MOUSE_PORT 0x60
#define MOUSE_STATUS 0x64

static volatile int mouse_x = 512;
static volatile int mouse_y = 384;
static volatile int mouse_buttons = 0;

static volatile int mouse_cycle = 0;
static volatile uint8_t mouse_packet[3];

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout-- && (inb(MOUSE_STATUS) & 0x02));
    } else {
        while (timeout-- && !(inb(MOUSE_STATUS) & 0x01));
    }
}

static void mouse_write(uint8_t val) {
    mouse_wait(0);
    outb(MOUSE_STATUS, 0xD4);
    mouse_wait(0);
    outb(MOUSE_PORT, val);
}

static uint8_t mouse_read(void) {
    mouse_wait(1);
    return inb(MOUSE_PORT);
}

void mouse_process_byte(uint8_t data) {
    switch (mouse_cycle) {
        case 0:
            if (!(data & 0x08)) { mouse_cycle = 0; return; }
            mouse_packet[0] = data;
            mouse_cycle = 1;
            break;
        case 1:
            mouse_packet[1] = data;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_packet[2] = data;
            mouse_cycle = 0;

            mouse_buttons = mouse_packet[0] & 0x07;

            int dx = (int)(int8_t)mouse_packet[1];
            int dy = -(int)(int8_t)mouse_packet[2];

            mouse_x += dx;
            mouse_y += dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > 1023) mouse_x = 1023;
            if (mouse_y > 767) mouse_y = 767;
            break;
    }
}

void mouse_handler(uint64_t frame) {
    (void)frame;
    uint8_t status = inb(MOUSE_STATUS);
    if (!(status & 0x01)) return;
    if (!(status & 0x20)) return;

    uint8_t data = inb(MOUSE_PORT);
    mouse_process_byte(data);
}

void mouse_poll(void) {
    asm volatile("cli");
    while (inb(MOUSE_STATUS) & 0x01) {
        int is_mouse = inb(MOUSE_STATUS) & 0x20;
        uint8_t data = inb(MOUSE_PORT);
        if (is_mouse)
            mouse_process_byte(data);
        else
            keyboard_process_scancode(data);
    }
    asm volatile("sti");
}

void mouse_init(void) {
    mouse_wait(0);
    outb(MOUSE_STATUS, 0xA8);

    mouse_wait(0);
    outb(MOUSE_STATUS, 0x20);
    uint8_t status = inb(MOUSE_PORT);
    status |= 0x02;
    mouse_wait(0);
    outb(MOUSE_STATUS, 0x60);
    mouse_wait(0);
    outb(MOUSE_PORT, status);

    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF4);
    mouse_read();

    irq_register_handler(MOUSE_IRQ, mouse_handler);
}

void mouse_get_state(int *x, int *y, int *buttons) {
    *x = mouse_x;
    *y = mouse_y;
    *buttons = mouse_buttons;
}

void mouse_set_position(int x, int y) {
    mouse_x = x;
    mouse_y = y;
}
