#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define UNIQOS_VERSION "1.0.0"
#define UNIQOS_NAME "UniqOS"

#define PANIC(msg) do { kprintf("PANIC: %s at %s:%d\n", msg, __FILE__, __LINE__); for(;;) asm("hlt"); } while(0)
#define assert(cond) do { if(!(cond)) PANIC("assertion failed: " #cond); } while(0)
#define kassert(cond, fmt, ...) do { if(!(cond)) { kprintf("ASSERT: " fmt " at %s:%d\n", ##__VA_ARGS__, __FILE__, __LINE__); for(;;) asm("hlt"); } } while(0)

void kprintf(const char *fmt, ...);
void kputchar(char c);
void kputs(const char *s);

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);

void gdt_init(void);
void idt_init(void);
void pic_init(void);
void pit_init(uint32_t frequency);
void keyboard_init(void);

void pmm_init(uint64_t mem_upper);
void *pmm_alloc(void);
void pmm_free(void *ptr);
uint64_t pmm_get_free_count(void);

void vmm_init(void);
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t vmm_get_phys(uint64_t virt);
void vmm_switch_page_table(uint64_t *pml4);

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t nmemb, size_t size);
void *krealloc(void *ptr, size_t size);

void scheduler_init(void);
void scheduler_yield(void);
int thread_create(void (*entry)(void *), void *arg);
void thread_exit(void);
void thread_sleep(uint64_t ms);

void timer_sleep(uint64_t ms);
uint64_t timer_get_ms(void);

void display_init(void);
int display_is_fb(void);
void display_put_pixel(int x, int y, uint32_t color);
void display_fill_rect(int x, int y, int w, int h, uint32_t color);
void display_present(void);
int display_get_width(void);
int display_get_height(void);
uint32_t *display_get_buffer(void);

int keyboard_getc(void);
int keyboard_scancode(void);

void syscall_init(void);

size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *dest, const char *src);
char *strstr(const char *haystack, const char *needle);
