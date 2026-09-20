#ifndef FORGE_TASK_H
#define FORGE_TASK_H

#include <stdbool.h>
#include <stdint.h>

#define FR_TASK_SLEEP_MAX_TICKS (UINT32_MAX / 2u)

typedef struct fr_task fr_task_t;
typedef fr_task_t *fr_task_handle_t;

typedef void (*fr_task_entry_t)(void *argument);

typedef uint8_t fr_task_priority_t;
typedef uint8_t fr_task_state_t;
typedef uint8_t fr_task_status_t;

enum {
    FR_TASK_STATE_INVALID = 0u,
    FR_TASK_STATE_CREATED,
    FR_TASK_STATE_READY,
    FR_TASK_STATE_RUNNING,
    FR_TASK_STATE_BLOCKED,
    FR_TASK_STATE_SUSPENDED
};

enum {
    FR_TASK_OK = 0u,
    FR_TASK_ERROR_INVALID_ARGUMENT,
    FR_TASK_ERROR_INVALID_PRIORITY,
    FR_TASK_ERROR_INVALID_STACK,
    FR_TASK_ERROR_NO_CAPACITY
};

typedef struct {
    fr_task_entry_t entry;
    void *argument;
    uint32_t *stack_memory;
    uint32_t stack_size_words;
    fr_task_priority_t priority;
} fr_task_config_t;

typedef struct {
    uint32_t id;
    fr_task_state_t state;
    fr_task_priority_t priority;
    fr_task_priority_t base_priority;

    uint32_t stack_size_words;

    uintptr_t stack_base;
    uintptr_t stack_top;
    uintptr_t saved_sp;
} fr_task_info_t;

fr_task_status_t fr_task_create(fr_task_handle_t *out_task, const fr_task_config_t *config);
uint32_t fr_task_count(void);
bool fr_task_get_info(fr_task_handle_t task, fr_task_info_t *out_info);

/*
 * Block the calling task for the requested number of kernel ticks.
 * Returns true after the task wakes and resumes.
 * Returns false for invalid usage or duration.
 */
bool fr_task_sleep(uint32_t delay_ticks);

void fr_task_yield(void);

#endif