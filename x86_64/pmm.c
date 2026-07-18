#include "pmm.h"
#include "kernel.h"

static uint64_t *bitmap = 0;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t last_alloc = 0;

#define PAGE_SIZE 4096
#define BITMAP_SIZE(pages) ((pages + 63) / 64)
#define PMM_BITMAP_ADDR 0x1000000

extern char __end[];

static inline void bitmap_set(int i) {
    bitmap[i / 64] |= (1ULL << (i % 64));
}

static inline void bitmap_clear(int i) {
    bitmap[i / 64] &= ~(1ULL << (i % 64));
}

static inline int bitmap_test(int i) {
    return (bitmap[i / 64] >> (i % 64)) & 1;
}

void pmm_init(uint64_t mem_upper) {
    total_pages = mem_upper / PAGE_SIZE;
    uint64_t bitmap_pages = (BITMAP_SIZE(total_pages) * 8 + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t bitmap_addr = PMM_BITMAP_ADDR;
    bitmap = (uint64_t *)bitmap_addr;

    uint64_t bitmap_entries = BITMAP_SIZE(total_pages);
    for (uint64_t i = 0; i < bitmap_entries; i++)
        bitmap[i] = 0;

    for (uint64_t i = 0; i < total_pages; i++)
        bitmap_clear(i);

    free_pages = total_pages;

    uint64_t kernel_end = (uint64_t)__end;
    uint64_t kernel_pages = (kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < kernel_pages; i++)
        pmm_alloc_page();

    uint64_t bitmap_start_page = bitmap_addr / PAGE_SIZE;
    for (uint64_t i = bitmap_start_page; i < bitmap_start_page + bitmap_pages; i++)
        bitmap_set(i);
    free_pages -= bitmap_pages;

    kprintf("[pmm] %llu MB free, bitmap at 0x%x, kernel_end=0x%x\n",
        free_pages * PAGE_SIZE / (1024*1024), bitmap_addr, kernel_end);
}

void *pmm_alloc_page(void) {
    for (uint64_t i = last_alloc; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            last_alloc = i + 1;
            return (void *)(i * PAGE_SIZE);
        }
    }
    for (uint64_t i = 0; i < last_alloc; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            last_alloc = i + 1;
            return (void *)(i * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void *ptr) {
    uint64_t addr = (uint64_t)ptr;
    if (addr % PAGE_SIZE) return;
    uint64_t page = addr / PAGE_SIZE;
    if (page >= total_pages) return;
    if (!bitmap_test(page)) return;
    bitmap_clear(page);
    free_pages++;
}

uint64_t pmm_get_free_count(void) {
    return free_pages;
}

void *pmm_alloc(void) {
    return pmm_alloc_page();
}

void pmm_free(void *ptr) {
    pmm_free_page(ptr);
}
