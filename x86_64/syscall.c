#include "syscall.h"
#include "kernel.h"
#include "idt.h"

#define SYSCALL_WRITE 0
#define SYSCALL_EXIT 1
#define SYSCALL_YIELD 2
#define SYSCALL_UPTIME 3

uint64_t syscall_handler(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3) {
    switch (n) {
        case SYSCALL_WRITE: {
            const char *buf = (const char *)a1;
            uint64_t len = a2;
            for (uint64_t i = 0; i < len; i++) kputchar(buf[i]);
            return len;
        }
        case SYSCALL_EXIT:
            thread_exit();
            return 0;
        case SYSCALL_YIELD:
            scheduler_yield();
            return 0;
        case SYSCALL_UPTIME:
            return timer_get_ms();
        default:
            kprintf("[syscall] unknown: %llu\n", n);
            return -1;
    }
}

static void syscall_isr(uint64_t frame) {
    uint64_t n = ((uint64_t*)frame)[0];
    uint64_t a1 = ((uint64_t*)frame)[1];
    uint64_t a2 = ((uint64_t*)frame)[2];
    uint64_t a3 = ((uint64_t*)frame)[3];
    uint64_t result = syscall_handler(n, a1, a2, a3);
    ((uint64_t*)frame)[0] = result;
}

void syscall_init(void) {
    kprintf("[syscall] syscall interface initialized\n");
}
