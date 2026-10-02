#ifndef FORGE_TASK_STACK_H
#define FORGE_TASK_STACK_H

#include <stdbool.h>
#include <stdint.h>

#include "forge/task.h"

#define FR_TASK_STACK_GUARD_WORDS 8u

typedef struct {
    uint32_t size_words;
    uint32_t guard_words;
    uint32_t usable_words;
    uint32_t minimum_free_words;
    uint32_t minimum_usable_free_words;
    uint32_t peak_used_words;

    uintptr_t stack_base;
    uintptr_t stack_top;
    uintptr_t saved_sp;

    bool guard_intact;
    bool saved_sp_in_bounds;
    bool saved_sp_aligned;
    bool overflow_detected;
} fr_task_stack_info_t;

/*
 * Inspect the historical stack high-water mark and guard region.
 *
 * The result is conservative: querying the currently running task may include
 * the stack usage of this diagnostic call itself in the reported peak usage.
 */
bool fr_task_stack_get_info(fr_task_handle_t task,
                            fr_task_stack_info_t *out_info);

#endif
