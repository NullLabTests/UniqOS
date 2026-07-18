#include "keyboard.h"
#include "idt.h"
#include "kernel.h"
#include "serial.h"

#define KEYBOARD_PORT 0x60
#define KEYBOARD_STATUS 0x64

static volatile char key_buffer[256];
static volatile int key_head = 0;
static volatile int key_tail = 0;

static const char scancode_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,
    0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0,
    ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,
    0, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0,
    ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0
};

static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;

void keyboard_process_scancode(uint8_t scancode) {
    uint8_t key = scancode & 0x7F;
    int released = scancode & 0x80;

    if (key == 0x2A || key == 0x36) { shift_pressed = !released; return; }
    if (key == 0x1D) { ctrl_pressed = !released; return; }
    if (key == 0x38) { alt_pressed = !released; return; }

    if (released) return;

    char c = 0;
    if (key < sizeof(scancode_ascii)) {
        c = shift_pressed ? scancode_shift[key] : scancode_ascii[key];
    }

    if (c) {
        int next = (key_head + 1) % 256;
        if (next != key_tail) {
            key_buffer[key_head] = c;
            key_head = next;
        }
    }
}

int keyboard_is_ctrl(void) {
    return ctrl_pressed;
}

void keyboard_handler(uint64_t frame) {
    (void)frame;
    uint8_t status = inb(KEYBOARD_STATUS);
    if (!(status & 0x01)) return;
    if (status & 0x20) return; // mouse data, ignore — polled separately

    uint8_t scancode = inb(KEYBOARD_PORT);
    keyboard_process_scancode(scancode);
}

void keyboard_init(void) {
    irq_register_handler(1, keyboard_handler);
}

int keyboard_getc(void) {
    if (key_tail == key_head) return -1;
    char c = key_buffer[key_tail];
    key_tail = (key_tail + 1) % 256;
    return c;
}

int keyboard_scancode(void) {
    return inb(KEYBOARD_PORT);
}
