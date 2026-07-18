#pragma once

#include <stdint.h>

void scheduler_init(void);
void scheduler_yield(void);
int thread_create(void (*entry)(void *), void *arg);
void thread_exit(void);
void thread_sleep(uint64_t ms);
