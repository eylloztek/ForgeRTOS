#ifndef FORGE_KERNEL_TASK_INTERNAL_H
#define FORGE_KERNEL_TASK_INTERNAL_H

#include <stdint.h>

#include "forge/task.h"

struct fr_task {
    uint32_t *saved_sp;

    uint32_t *stack_base;
    uint32_t *stack_top;

    fr_task_entry_t entry;
    void *argument;

    uint32_t stack_size_words;
    uint32_t id;

    fr_task_priority_t priority;
    fr_task_priority_t base_priority;
    fr_task_state_t state;
};

#endif