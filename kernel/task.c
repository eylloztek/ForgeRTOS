#include <stddef.h>
#include <stdint.h>

#include "forge/assert.h"
#include "forge/config.h"
#include "forge/critical.h"
#include "forge/kernel/port.h"
#include "forge/kernel/task_internal.h"
#include "forge/kernel/trace_internal.h"
#include "forge/task.h"
#include "forge/task_stack.h"

static fr_task_t g_fr_task_pool[FR_CONFIG_MAX_TASKS];
static volatile uint32_t g_fr_task_count;

#define FR_TASK_STACK_FILL_PATTERN 0xA5A5A5A5u

_Static_assert(FR_CONFIG_MAX_TASKS > 0u, "ForgeRTOS must support at least one task");
_Static_assert(FR_CONFIG_MAX_TASK_PRIORITY <= UINT8_MAX, "Task priority must fit fr_task_priority_t");
_Static_assert(FR_CONFIG_MIN_TASK_STACK_WORDS > FR_TASK_STACK_GUARD_WORDS,
               "Minimum task stack must be larger than the stack guard");

static bool fr_task_stack_is_valid(const fr_task_config_t *config) {
    if (config->stack_memory == NULL) {
        return false;
    }

    if (config->stack_size_words < FR_CONFIG_MIN_TASK_STACK_WORDS) {
        return false;
    }

    if ((config->stack_size_words & 1u) != 0u) {
        return false;
    }

    const uintptr_t stack_base = (uintptr_t)config->stack_memory;

    if ((stack_base & 0x7u) != 0u) {
        return false;
    }

    if ((uintptr_t)config->stack_size_words > (UINTPTR_MAX / sizeof(uint32_t))) {
        return false;
    }

    const uintptr_t stack_size_bytes = (uintptr_t)config->stack_size_words * sizeof(uint32_t);

    if (stack_base > (UINTPTR_MAX - stack_size_bytes)) {
        return false;
    }

    const uintptr_t stack_top = stack_base + stack_size_bytes;

    return (stack_top & 0x7u) == 0u;
}

static uint32_t *fr_task_calculate_stack_top(const fr_task_config_t *config) {
    const uintptr_t stack_base = (uintptr_t)config->stack_memory;
    const uintptr_t stack_size_bytes = (uintptr_t)config->stack_size_words * sizeof(uint32_t);

    return (uint32_t *)(stack_base + stack_size_bytes);
}

static void fr_task_stack_fill(uint32_t *stack_base,
                               uint32_t stack_size_words) {
    for (uint32_t i = 0u; i < stack_size_words; ++i) {
        stack_base[i] = FR_TASK_STACK_FILL_PATTERN;
    }
}

static uint32_t fr_task_stack_minimum_free_words(
    const volatile uint32_t *stack_base,
    uint32_t stack_size_words) {
    uint32_t free_words = 0u;

    while ((free_words < stack_size_words) &&
           (stack_base[free_words] == FR_TASK_STACK_FILL_PATTERN)) {
        ++free_words;
    }

    return free_words;
}

static bool fr_task_stack_guard_intact(
    const volatile uint32_t *stack_base) {
    for (uint32_t i = 0u; i < FR_TASK_STACK_GUARD_WORDS; ++i) {
        if (stack_base[i] != FR_TASK_STACK_FILL_PATTERN) {
            return false;
        }
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

    const fr_critical_state_t reserve_state = fr_critical_enter();

    if (g_fr_task_count >= FR_CONFIG_MAX_TASKS) {
        fr_critical_exit(reserve_state);
        return FR_TASK_ERROR_NO_CAPACITY;
    }

    const uint32_t index = g_fr_task_count;
    fr_task_t *const task = &g_fr_task_pool[index];

    task->saved_sp = NULL;
    task->stack_base = config->stack_memory;
    task->stack_top = fr_task_calculate_stack_top(config);

    task->entry = config->entry;
    task->argument = config->argument;

    task->stack_size_words = config->stack_size_words;
    task->id = index + 1u;

    task->priority = config->priority;
    task->base_priority = config->priority;

    task->state = FR_TASK_STATE_CREATED;

    task->wait_flags_mask = 0u;
    task->wait_flags_result = 0u;
    task->wait_flags_all = false;
    task->wait_flags_clear_on_exit = false;

    g_fr_task_count = index + 1u;

    fr_critical_exit(reserve_state);

    /*
     * Fill the complete stack before the synthetic initial context is built.
     * The untouched prefix becomes the historical stack high-water mark.
     */
    fr_task_stack_fill(task->stack_base, task->stack_size_words);

    uint32_t *const initial_sp =
        fr_port_task_stack_init(task->stack_top, task->entry, task->argument);

    const uintptr_t initial_sp_address = (uintptr_t)initial_sp;
    const uintptr_t stack_base_address = (uintptr_t)task->stack_base;
    const uintptr_t stack_top_address = (uintptr_t)task->stack_top;

    FR_ASSERT(initial_sp != NULL);
    FR_ASSERT(initial_sp_address >= stack_base_address);
    FR_ASSERT(initial_sp_address <= stack_top_address);
    FR_ASSERT((initial_sp_address & 0x7u) == 0u);

    const fr_critical_state_t publish_state = fr_critical_enter();

    task->saved_sp = initial_sp;
    task->wait_deadline = 0u;
    task->wait_object = NULL;
    task->wait_reason = FR_WAIT_REASON_NONE;
    task->wait_result = FR_WAIT_RESULT_PENDING;
    task->wait_has_deadline = false;
    task->state = FR_TASK_STATE_READY;
    *out_task = task;

    FR_ASSERT(task->saved_sp != NULL);
    FR_ASSERT(task->state == FR_TASK_STATE_READY);
    FR_ASSERT(task->wait_reason == FR_WAIT_REASON_NONE);
    FR_ASSERT(task->wait_object == NULL);

    fr_trace_record_locked(FR_TRACE_EVENT_TASK_CREATE,
                    task->id,
                    (uint32_t)task->base_priority,
                    task->stack_size_words);

    fr_critical_exit(publish_state);

    return FR_TASK_OK;
}

uint32_t fr_task_count(void) {
    return g_fr_task_count;
}

bool fr_task_stack_get_info(fr_task_handle_t task,
                            fr_task_stack_info_t *out_info) {
    if (out_info == NULL) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();

    if (!fr_task_handle_is_valid(task)) {
        fr_critical_exit(critical_state);
        return false;
    }

    const uint32_t size_words = task->stack_size_words;
    const uint32_t minimum_free_words =
        fr_task_stack_minimum_free_words(task->stack_base, size_words);
    const bool guard_intact =
        fr_task_stack_guard_intact(task->stack_base);

    const uintptr_t stack_base = (uintptr_t)task->stack_base;
    const uintptr_t stack_top = (uintptr_t)task->stack_top;
    const uintptr_t saved_sp = (uintptr_t)task->saved_sp;

    const bool saved_sp_in_bounds =
        (saved_sp >= stack_base) && (saved_sp <= stack_top);
    const bool saved_sp_aligned = (saved_sp & 0x7u) == 0u;

    out_info->size_words = size_words;
    out_info->guard_words = FR_TASK_STACK_GUARD_WORDS;
    out_info->usable_words = size_words - FR_TASK_STACK_GUARD_WORDS;
    out_info->minimum_free_words = minimum_free_words;
    out_info->minimum_usable_free_words =
        (minimum_free_words > FR_TASK_STACK_GUARD_WORDS)
            ? (minimum_free_words - FR_TASK_STACK_GUARD_WORDS)
            : 0u;
    out_info->peak_used_words = size_words - minimum_free_words;

    out_info->stack_base = stack_base;
    out_info->stack_top = stack_top;
    out_info->saved_sp = saved_sp;

    out_info->guard_intact = guard_intact;
    out_info->saved_sp_in_bounds = saved_sp_in_bounds;
    out_info->saved_sp_aligned = saved_sp_aligned;
    out_info->overflow_detected =
        !guard_intact || !saved_sp_in_bounds;

    fr_critical_exit(critical_state);
    return true;
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

uint32_t fr_task_internal_count(void) {
    return g_fr_task_count;
}

fr_task_t *fr_task_internal_at(uint32_t index) {
    if (index >= g_fr_task_count) {
        return NULL;
    }

    return &g_fr_task_pool[index];
}