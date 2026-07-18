#include "pci.h"
#include "serial.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

static uint16_t pci_read_vendor(uint8_t bus, uint8_t slot, uint8_t func) {
    return pci_read_config(bus, slot, func, 0) & 0xFFFF;
}

static uint16_t pci_read_device(uint8_t bus, uint8_t slot, uint8_t func) {
    return (pci_read_config(bus, slot, func, 0) >> 16) & 0xFFFF;
}

int pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint16_t v = pci_read_vendor(bus, slot, func);
                if (v == 0xFFFF) {
                    if (func == 0) break;
                    continue;
                }
                uint16_t d = pci_read_device(bus, slot, func);
                if (v == vendor && d == device) {
                    out->vendor_id = v;
                    out->device_id = d;
                    out->bus = bus;
                    out->slot = slot;
                    out->func = func;
                    uint32_t cc = pci_read_config(bus, slot, func, 0x08);
                    out->class_code = (cc >> 24) & 0xFF;
                    out->subclass = (cc >> 16) & 0xFF;
                    out->prog_if = (cc >> 8) & 0xFF;
                    return 1;
                }
                if (func == 0 && !(pci_read_config(bus, slot, 0, 0x0C) & 0x800000)) break;
            }
        }
    }
    return 0;
}

int pci_find_class(uint8_t class, uint8_t subclass, pci_device_t *out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint16_t v = pci_read_vendor(bus, slot, func);
                if (v == 0xFFFF) {
                    if (func == 0) break;
                    continue;
                }
                uint32_t cc = pci_read_config(bus, slot, func, 0x08);
                if (((cc >> 24) & 0xFF) == class && ((cc >> 16) & 0xFF) == subclass) {
                    out->vendor_id = v;
                    out->device_id = (pci_read_config(bus, slot, func, 0) >> 16) & 0xFFFF;
                    out->bus = bus;
                    out->slot = slot;
                    out->func = func;
                    out->class_code = class;
                    out->subclass = subclass;
                    out->prog_if = (cc >> 8) & 0xFF;
                    return 1;
                }
                if (func == 0 && !(pci_read_config(bus, slot, 0, 0x0C) & 0x800000)) break;
            }
        }
    }
    return 0;
}

uint64_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar) {
    uint32_t val = pci_read_config(bus, slot, func, 0x10 + bar * 4);
    if (val & 1) {
        return val & 0xFFFC;
    }
    uint32_t high = pci_read_config(bus, slot, func, 0x10 + bar * 4 + 4);
    return ((uint64_t)high << 32) | (val & 0xFFFFFFF0);
}
