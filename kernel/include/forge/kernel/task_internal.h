#ifndef FORGE_KERNEL_TASK_INTERNAL_H
#define FORGE_KERNEL_TASK_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>

#include "forge/task.h"
#include "forge/kernel/wait_internal.h"

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

    uint32_t wait_deadline;
    fr_wait_reason_t wait_reason;
    volatile fr_wait_result_t wait_result;
    bool wait_has_deadline;
};

uint32_t fr_task_internal_count(void);
fr_task_t *fr_task_internal_at(uint32_t index);

#endif