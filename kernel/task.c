#include <stddef.h>
#include <stdint.h>

#include "forge/config.h"
#include "forge/critical.h"
#include "forge/kernel/task_internal.h"
#include "forge/task.h"

static fr_task_t g_fr_task_pool[FR_CONFIG_MAX_TASKS];
static volatile uint32_t g_fr_task_count;

static bool fr_task_stack_is_valid(const fr_task_config_t *config) {
    const uintptr_t stack_address = (uintptr_t)config->stack_memory;

    if (config->stack_memory == NULL) {
        return false;
    }

    if (config->stack_size_words < FR_CONFIG_MIN_TASK_STACK_WORDS) {
        return false;
    }

    if ((config->stack_size_words & 1u) != 0u) {
        return false;
    }

    if ((stack_address & 0x7u) != 0u) {
        return false;
    }

    return true;
}

static bool fr_task_handle_is_valid(fr_task_handle_t task) {
    if (task == NULL) {
        return false;
    }

    const uintptr_t pool_begin = (uintptr_t)&g_fr_task_pool[0];
    const uintptr_t pool_end = (uintptr_t)&g_fr_task_pool[FR_CONFIG_MAX_TASKS];
    const uintptr_t task_address = (uintptr_t)task;

    if ((task_address < pool_begin) || (task_address >= pool_end)) {
        return false;
    }

    const uintptr_t offset = task_address - pool_begin;

    if ((offset % sizeof(fr_task_t)) != 0u) {
        return false;
    }

    const uint32_t index = (uint32_t)(offset / sizeof(fr_task_t));

    if (index >= g_fr_task_count) {
        return false;
    }

    return g_fr_task_pool[index].id != 0u;
}

fr_task_status_t fr_task_create(fr_task_handle_t *out_task, const fr_task_config_t *config) {
    if ((out_task == NULL) || (config == NULL)) {
        return FR_TASK_ERROR_INVALID_ARGUMENT;
    }

    *out_task = NULL;

    if (config->entry == NULL) {
        return FR_TASK_ERROR_INVALID_ARGUMENT;
    }

    if (config->priority > FR_CONFIG_MAX_TASK_PRIORITY) {
        return FR_TASK_ERROR_INVALID_PRIORITY;
    }

    if (!fr_task_stack_is_valid(config)) {
        return FR_TASK_ERROR_INVALID_STACK;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();

    if (g_fr_task_count >= FR_CONFIG_MAX_TASKS) {
        fr_critical_exit(critical_state);
        return FR_TASK_ERROR_NO_CAPACITY;
    }

    const uint32_t index = g_fr_task_count;
    fr_task_t *const task = &g_fr_task_pool[index];

    task->saved_sp = NULL;

    task->stack_base = config->stack_memory;
    task->stack_top = config->stack_memory + config->stack_size_words;

    task->entry = config->entry;
    task->argument = config->argument;

    task->stack_size_words = config->stack_size_words;
    task->id = index + 1u;

    task->priority = config->priority;
    task->base_priority = config->priority;

    task->state = FR_TASK_STATE_CREATED;

    g_fr_task_count = index + 1u;
    *out_task = task;

    fr_critical_exit(critical_state);

    return FR_TASK_OK;
}

uint32_t fr_task_count(void) {
    return g_fr_task_count;
}

bool fr_task_get_info(fr_task_handle_t task, fr_task_info_t *out_info) {
    if (out_info == NULL) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();

    if (!fr_task_handle_is_valid(task)) {
        fr_critical_exit(critical_state);
        return false;
    }

    out_info->id = task->id;

    out_info->state = task->state;

    out_info->priority = task->priority;
    out_info->base_priority = task->base_priority;

    out_info->stack_size_words = task->stack_size_words;

    out_info->stack_base = (uintptr_t)task->stack_base;
    out_info->stack_top = (uintptr_t)task->stack_top;
    out_info->saved_sp = (uintptr_t)task->saved_sp;

    fr_critical_exit(critical_state);

    return true;
}