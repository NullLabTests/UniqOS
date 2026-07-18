#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "kernel.h"

#define HEAP_START 0x100000000
#define HEAP_INITIAL_SIZE 0x100000
#define HEAP_MAGIC 0xDEADBEEF

typedef struct heap_block {
    uint32_t magic;
    uint32_t size;
    struct heap_block *next;
    struct heap_block *prev;
    int free;
} heap_block_t;

static heap_block_t *heap_base = 0;
static uint64_t heap_current_max = 0;

void heap_init(void) {
    heap_base = (heap_block_t *)HEAP_START;
    heap_current_max = HEAP_START;

    for (uint64_t addr = HEAP_START; addr < HEAP_START + HEAP_INITIAL_SIZE; addr += 0x1000)
        vmm_map_page(addr, (uint64_t)pmm_alloc_page(), 3);

    heap_current_max = HEAP_START + HEAP_INITIAL_SIZE;

    heap_base->magic = HEAP_MAGIC;
    heap_base->size = heap_current_max - HEAP_START - sizeof(heap_block_t);
    heap_base->next = 0;
    heap_base->prev = 0;
    heap_base->free = 1;
}

static void heap_expand(uint64_t size) {
    uint64_t pages_needed = (size + 0xFFF) / 0x1000;
    for (uint64_t i = 0; i < pages_needed; i++) {
        vmm_map_page(heap_current_max, (uint64_t)pmm_alloc_page(), 3);
        heap_current_max += 0x1000;
    }
}

void *kmalloc(size_t size) {
    if (!size) return 0;
    if (size < 16) size = 16;
    size = (size + 7) & ~7;

    heap_block_t *block = heap_base;
    while (block) {
        if (block->magic != HEAP_MAGIC) PANIC("Heap corruption");
        if (block->free && block->size >= size) {
            if (block->size > size + sizeof(heap_block_t) + 16) {
                heap_block_t *new_block = (heap_block_t *)((uint64_t)block + sizeof(heap_block_t) + size);
                new_block->magic = HEAP_MAGIC;
                new_block->size = block->size - size - sizeof(heap_block_t);
                new_block->next = block->next;
                new_block->prev = block;
                new_block->free = 1;
                block->size = size;
                block->next = new_block;
                if (new_block->next) new_block->next->prev = new_block;
            }
            block->free = 0;
            memset((void*)((uint64_t)block + sizeof(heap_block_t)), 0, size);
            return (void *)((uint64_t)block + sizeof(heap_block_t));
        }
        block = block->next;
    }

    uint64_t new_size = size + sizeof(heap_block_t) + 0x10000;
    heap_expand(new_size);

    heap_block_t *last = heap_base;
    while (last->next) last = last->next;

    heap_block_t *new_block = (heap_block_t *)((uint64_t)last + sizeof(heap_block_t) + last->size);
    new_block->magic = HEAP_MAGIC;
    new_block->size = (heap_current_max - (uint64_t)new_block - sizeof(heap_block_t));
    new_block->next = 0;
    new_block->prev = last;
    new_block->free = 1;
    last->next = new_block;

    if (new_block->size >= size) {
        heap_block_t *split = (heap_block_t *)((uint64_t)new_block + sizeof(heap_block_t) + size);
        split->magic = HEAP_MAGIC;
        split->size = new_block->size - size - sizeof(heap_block_t);
        split->next = 0;
        split->prev = new_block;
        split->free = 1;
        new_block->size = size;
        new_block->next = split;
        new_block->free = 0;
        memset((void*)((uint64_t)new_block + sizeof(heap_block_t)), 0, size);
        return (void *)((uint64_t)new_block + sizeof(heap_block_t));
    }

    return 0;
}

void kfree(void *ptr) {
    if (!ptr) return;
    heap_block_t *block = (heap_block_t *)((uint64_t)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) PANIC("Heap corruption on free");
    block->free = 1;

    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }
    if (block->prev && block->prev->free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

void *kcalloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (!size) { kfree(ptr); return 0; }
    heap_block_t *block = (heap_block_t *)((uint64_t)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) PANIC("Heap corruption on realloc");
    void *new_ptr = kmalloc(size);
    if (new_ptr) {
        size_t copy_size = block->size < size ? block->size : size;
        memcpy(new_ptr, ptr, copy_size);
    }
    kfree(ptr);
    return new_ptr;
}
