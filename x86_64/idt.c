#include "idt.h"
#include "kernel.h"
#include "serial.h"

static idt_entry_t idt_entries[256];
static idt_ptr_t idt_ptr;

extern void (*isr_stub_table[32])(void);
extern void (*irq_stub_table[16])(void);

static void idt_set_entry(int i, uint64_t base, uint16_t selector, uint8_t flags) {
    idt_entries[i].base_low = base & 0xFFFF;
    idt_entries[i].selector = selector;
    idt_entries[i].ist = 0;
    idt_entries[i].flags = flags;
    idt_entries[i].base_mid = (base >> 16) & 0xFFFF;
    idt_entries[i].base_high = (base >> 32) & 0xFFFFFFFF;
    idt_entries[i].reserved = 0;
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entries) - 1;
    idt_ptr.base = (uint64_t)&idt_entries;

    for (int i = 0; i < 256; i++)
        idt_set_entry(i, (uint64_t)isr_stub_table[0], 0x08, 0x8E);

    for (int i = 0; i < 32; i++)
        idt_set_entry(i, (uint64_t)isr_stub_table[i], 0x08, 0x8E);

    for (int i = 0; i < 16; i++)
        idt_set_entry(32 + i, (uint64_t)irq_stub_table[i], 0x08, 0x8E);

    asm volatile("lidt %0" : : "m"(idt_ptr));
}

const char *exception_names[32] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode",
    "No Coprocessor", "Double Fault", "Coprocessor Segment Overrun",
    "Bad TSS", "Segment Not Present", "Stack Fault", "General Protection Fault",
    "Page Fault", "Unknown Interrupt", "Coprocessor Fault", "Alignment Check",
    "Machine Check", "SIMD Floating Point Exception", "Virtualization Exception",
    "Control Protection Exception", "Reserved", "Hypervisor Injection",
    "VMM Communication", "Security Exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved"
};

void isr_handler(uint64_t *frame) {
    uint64_t int_no = frame[15];
    kprintf("[ISR] exception %d: %s\n", int_no, exception_names[int_no]);

    if (int_no == 14) {
        uint64_t cr2;
        asm("mov %%cr2, %0" : "=r"(cr2));
        kprintf("[ISR] page fault at RIP=0x%x, address 0x%x\n",
            frame[17], cr2);
    }

    PANIC("Unhandled exception");
}

void *irq_routines[16] = {0};

void irq_register_handler(int irq, void (*handler)(uint64_t)) {
    irq_routines[irq] = (void*)handler;
}

void irq_handler(uint64_t *frame) {
    int vec = frame[15] & 0xFF;
    int irq = vec - 32;
    if (irq >= 0 && irq < 16 && irq_routines[irq]) {
        ((void(*)(uint64_t))irq_routines[irq])((uint64_t)frame);
    }
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}
