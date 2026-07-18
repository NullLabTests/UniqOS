#include "scheduler.h"
#include "kernel.h"
#include "idt.h"

typedef struct task {
    uint64_t id;
    uint64_t rsp;
    uint64_t rip;
    uint64_t *page_table;
    int state;
    uint64_t sleep_until;
    struct task *next;
    char name[32];
} task_t;

#define TASK_READY 0
#define TASK_RUNNING 1
#define TASK_SLEEPING 2
#define TASK_EXITED 3

#define STACK_SIZE 16384

static task_t task_pool[64];
static int task_count = 0;
static task_t *current_task = 0;
static task_t *idle_task = 0;
static task_t *ready_queue = 0;
static uint64_t next_id = 1;

void scheduler_tick(void) {
    scheduler_yield();
}

static void push_ready(task_t *task) {
    task->state = TASK_READY;
    if (!ready_queue) {
        ready_queue = task;
        task->next = task;
    } else {
        task->next = ready_queue->next;
        ready_queue->next = task;
        ready_queue = task;
    }
}

static task_t *pop_ready(void) {
    if (!ready_queue) return 0;
    task_t *task = ready_queue->next;
    if (task == ready_queue) {
        ready_queue = 0;
    } else {
        ready_queue->next = task->next;
    }
    task->next = 0;
    task->state = TASK_RUNNING;
    return task;
}

void scheduler_init(void) {
    kprintf("[sched] scheduler up\n");
}

int thread_create(void (*entry)(void *), void *arg) {
    if (task_count >= 64) return -1;

    task_t *task = &task_pool[task_count++];
    task->id = next_id++;
    task->state = TASK_READY;
    task->page_table = 0;
    task->sleep_until = 0;

    uint64_t *stack = (uint64_t *)kmalloc(STACK_SIZE);
    if (!stack) return -1;
    uint64_t *rsp = stack + STACK_SIZE / 8;

    *--rsp = (uint64_t)arg;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = (uint64_t)thread_exit;
    *--rsp = (uint64_t)entry;
    task->rsp = (uint64_t)rsp;

    const char *name = "thread";
    int i = 0;
    for (; name[i] && i < 31; i++) task->name[i] = name[i];
    task->name[i] = 0;

    push_ready(task);
    return task->id;
}

void thread_exit(void) {
    if (current_task) current_task->state = TASK_EXITED;
    scheduler_yield();
    for (;;) asm("hlt");
}

void thread_sleep(uint64_t ms) {
    if (current_task) {
        current_task->sleep_until = timer_get_ms() + ms;
        current_task->state = TASK_SLEEPING;
    }
    scheduler_yield();
}

void scheduler_yield(void) {
    uint64_t current_rsp = 0;
    asm("mov %%rsp, %0" : "=r"(current_rsp));

    if (current_task) {
        current_task->rsp = current_rsp;
        if (current_task->state == TASK_RUNNING)
            push_ready(current_task);
    }

    uint64_t now = timer_get_ms();
    task_t *new_task = 0;

    while (ready_queue) {
        task_t *candidate = ready_queue->next;
        if (candidate->state == TASK_EXITED) {
            task_t *next = candidate->next;
            if (candidate == ready_queue) ready_queue = 0;
            else ready_queue->next = next;
            continue;
        }
        if (candidate->state == TASK_SLEEPING && candidate->sleep_until > now) {
            if (ready_queue && candidate == ready_queue) break;
            task_t *next = candidate->next;
            if (ready_queue) ready_queue->next = next;
            continue;
        }
        new_task = pop_ready();
        break;
    }

    if (!new_task) {
        uint64_t cr3;
        asm("mov %%cr3, %0" : "=r"(cr3));
        current_task = 0;
        asm volatile("mov %0, %%rsp; sti" : : "r"(idle_task ? idle_task->rsp : current_rsp));
        for (;;) asm("hlt");
    }

    current_task = new_task;

    if (new_task->page_table)
        asm volatile("mov %0, %%cr3" : : "r"(new_task->page_table) : "memory");

    __asm__ __volatile__(
        "mov %0, %%rsp; sti"
        : : "r"(new_task->rsp));
}
