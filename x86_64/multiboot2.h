#pragma once

#include <stdint.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

#define MULTIBOOT2_TAG_TYPE_END      0
#define MULTIBOOT2_TAG_TYPE_CMDLINE  1
#define MULTIBOOT2_TAG_TYPE_MODULE   3
#define MULTIBOOT2_TAG_TYPE_MMAP     6
#define MULTIBOOT2_TAG_TYPE_FB       8

#define MULTIBOOT2_MEMORY_AVAILABLE      1
#define MULTIBOOT2_MEMORY_RESERVED       2
#define MULTIBOOT2_MEMORY_ACPI_RECLAIM   3
#define MULTIBOOT2_MEMORY_NVS            4
#define MULTIBOOT2_MEMORY_BADRAM         5

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) multiboot2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) multiboot2_info_t;

typedef struct {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) multiboot2_mmap_entry_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    multiboot2_mmap_entry_t entries[];
} __attribute__((packed)) multiboot2_tag_mmap_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t fb_addr;
    uint32_t fb_pitch;
    uint32_t fb_width;
    uint32_t fb_height;
    uint8_t  fb_bpp;
    uint8_t  fb_type;
    uint16_t reserved;
} __attribute__((packed)) multiboot2_tag_fb_t;
