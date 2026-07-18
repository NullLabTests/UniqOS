#pragma once

#include <stdint.h>

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
} pci_device_t;

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
int pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out);
int pci_find_class(uint8_t class, uint8_t subclass, pci_device_t *out);
uint64_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar);
