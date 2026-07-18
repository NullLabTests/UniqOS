#include "pic.h"
#include "serial.h"

#define PIC1 0x20
#define PIC2 0xA0
#define ICW1 0x11
#define ICW4 0x01

void pic_init(void) {
    outb(PIC1, ICW1);
    outb(PIC2, ICW1);
    outb(PIC1 + 1, 0x20);
    outb(PIC2 + 1, 0x28);
    outb(PIC1 + 1, 0x04);
    outb(PIC2 + 1, 0x02);
    outb(PIC1 + 1, ICW4);
    outb(PIC2 + 1, ICW4);
    outb(PIC1 + 1, 0xF8);
    outb(PIC2 + 1, 0xFF); // mask all slave IRQs
}
