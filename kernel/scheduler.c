#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/config.h"
#include "forge/critical.h"
#include "forge/kernel/port.h"
#include "forge/kernel/scheduler_internal.h"
#include "forge/kernel/task_internal.h"
#include "forge/scheduler.h"
#include "forge/task.h"

_Static_assert(FR_CONFIG_IDLE_STACK_WORDS >= FR_CONFIG_MIN_TASK_STACK_WORDS,
               "Idle stack is smaller than the minimum task stack");

_Static_assert((FR_CONFIG_IDLE_STACK_WORDS % 2u) == 0u,
               "Idle stack size must preserve 8-byte alignment");

static _Alignas(8) uint32_t g_fr_scheduler_idle_stack[FR_CONFIG_IDLE_STACK_WORDS];
static fr_task_t g_fr_scheduler_idle_task;

static fr_task_t *volatile g_fr_scheduler_current_task;
static volatile bool g_fr_scheduler_running;
static volatile uint32_t g_fr_scheduler_context_switch_count;

/* Debug counters; not part of the public scheduler API. */
volatile uint32_t g_fr_scheduler_idle_entry_count;
volatile uint32_t g_fr_scheduler_idle_iterations;

static void fr_scheduler_idle_entry(void *argument) {
    (void)argument;

    ++g_fr_scheduler_idle_entry_count;

    while (1) {
        ++g_fr_scheduler_idle_iterations;

        __asm volatile(
            "dsb\n"
            "wfi\n"
            :
            :
            : "memory"
        );
    }
}

static void fr_scheduler_initialize_idle(void) {
    fr_task_t *const idle = &g_fr_scheduler_idle_task;

    idle->stack_base = g_fr_scheduler_idle_stack;
    idle->stack_top = &g_fr_scheduler_idle_stack[FR_CONFIG_IDLE_STACK_WORDS];
    idle->stack_size_words = FR_CONFIG_IDLE_STACK_WORDS;

    idle->entry = fr_scheduler_idle_entry;
    idle->argument = NULL;

    idle->id = 0u;
    idle->priority = 0u;
    idle->base_priority = 0u;

    idle->saved_sp = fr_port_task_stack_init(idle->stack_top,
                                             idle->entry,
                                             idle->argument);

    idle->state = FR_TASK_STATE_READY;
}

static fr_task_t *fr_scheduler_select_highest_ready(void) {
    fr_task_t *selected = NULL;
    const uint32_t task_count = fr_task_internal_count();

    for (uint32_t i = 0u; i < task_count; ++i) {
        fr_task_t *const task = fr_task_internal_at(i);

        if ((task == NULL) || (task->state != FR_TASK_STATE_READY)) {
            continue;
        }

        if ((selected == NULL) || (task->priority > selected->priority)) {
            selected = task;
        }
    }

    return selected;
}

static fr_task_t *fr_scheduler_select_next(fr_task_t *current_task) {
    if (current_task == NULL) {
        return NULL;
    }

    /*
     * The idle task is outside the normal task registry.
     * Any READY user task, including priority 0, outranks idle.
     */
    if (current_task == &g_fr_scheduler_idle_task) {
        fr_task_t *const ready_task = fr_scheduler_select_highest_ready();
        return (ready_task != NULL) ? ready_task : current_task;
    }

    if ((current_task->state != FR_TASK_STATE_RUNNING) &&
        (current_task->state != FR_TASK_STATE_BLOCKED)) {
        return NULL;
    }

    const uint32_t task_count = fr_task_internal_count();
    uint32_t current_index = task_count;

    for (uint32_t i = 0u; i < task_count; ++i) {
        if (fr_task_internal_at(i) == current_task) {
            current_index = i;
            break;
        }
    }

    if (current_index == task_count) {
        return NULL;
    }

    fr_task_t *selected =
        (current_task->state == FR_TASK_STATE_RUNNING) ? current_task : NULL;

    for (uint32_t offset = 1u; offset < task_count; ++offset) {
        uint32_t index = current_index + offset;

        if (index >= task_count) {
            index -= task_count;
        }

        fr_task_t *const candidate = fr_task_internal_at(index);

        if ((candidate == NULL) || (candidate->state != FR_TASK_STATE_READY)) {
            continue;
        }

        if ((selected == NULL) ||
            (candidate->priority > selected->priority) ||
            ((candidate->priority == selected->priority) &&
             (selected == current_task))) {
            selected = candidate;
        }
    }

    return (selected != NULL) ? selected : &g_fr_scheduler_idle_task;
}

fr_scheduler_status_t fr_scheduler_start(void) {
    const fr_critical_state_t critical_state = fr_critical_enter();

    if (g_fr_scheduler_running) {
        fr_critical_exit(critical_state);
        return FR_SCHEDULER_ERROR_ALREADY_RUNNING;
    }

    fr_scheduler_initialize_idle();

    fr_task_t *first_task = fr_scheduler_select_highest_ready();

    if (first_task == NULL) {
        first_task = &g_fr_scheduler_idle_task;
    }

    fr_port_scheduler_init();

    first_task->state = FR_TASK_STATE_RUNNING;
    g_fr_scheduler_current_task = first_task;
    g_fr_scheduler_context_switch_count = 0u;
    g_fr_scheduler_running = true;

    fr_port_start_first_task(critical_state);
}

bool fr_scheduler_is_running(void) {
    return g_fr_scheduler_running;
}

fr_task_handle_t fr_scheduler_current_task(void) {
    fr_task_t *const current_task = g_fr_scheduler_current_task;

    /*
     * Idle is kernel-owned and is not a registered user-task handle.
     */
    return (current_task == &g_fr_scheduler_idle_task) ? NULL : current_task;
}

void fr_scheduler_tick_isr(void) {
    if (!g_fr_scheduler_running) {
        return;
    }

    fr_task_t *const current_task = g_fr_scheduler_current_task;

    if (current_task == NULL) {
        return;
    }

    fr_task_t *const next_task = fr_scheduler_select_next(current_task);

    if ((next_task != NULL) && (next_task != current_task)) {
        fr_port_request_context_switch();
    }
}

uint32_t *fr_scheduler_current_saved_sp(void) {
    fr_task_t *const current_task = g_fr_scheduler_current_task;

    return (current_task != NULL) ? current_task->saved_sp : NULL;
}

uint32_t *fr_scheduler_switch_from_isr(uint32_t *current_saved_sp) {
    if (current_saved_sp == NULL) {
        return NULL;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();
    fr_task_t *const current_task = g_fr_scheduler_current_task;

    if ((!g_fr_scheduler_running) ||
        (current_task == NULL) ||
        ((current_task->state != FR_TASK_STATE_RUNNING) &&
         (current_task->state != FR_TASK_STATE_BLOCKED))) {
        fr_critical_exit(critical_state);
        return NULL;
    }

    current_task->saved_sp = current_saved_sp;

    fr_task_t *const next_task = fr_scheduler_select_next(current_task);

    if (next_task == NULL) {
        fr_critical_exit(critical_state);
        return NULL;
    }

    if (next_task != current_task) {
        if (current_task->state == FR_TASK_STATE_RUNNING) {
            current_task->state = FR_TASK_STATE_READY;
        }

        next_task->state = FR_TASK_STATE_RUNNING;
        g_fr_scheduler_current_task = next_task;
        ++g_fr_scheduler_context_switch_count;
    }

    uint32_t *const next_saved_sp = next_task->saved_sp;

    fr_critical_exit(critical_state);

    return next_saved_sp;
}

void fr_task_yield(void) {
    const fr_critical_state_t critical_state = fr_critical_enter();
    fr_task_t *const current_task = g_fr_scheduler_current_task;

    bool should_yield = false;

    if (g_fr_scheduler_running &&
        (current_task != NULL) &&
        (current_task != &g_fr_scheduler_idle_task)) {
        fr_task_t *const next_task = fr_scheduler_select_next(current_task);
        should_yield = (next_task != NULL) && (next_task != current_task);
    }

    fr_critical_exit(critical_state);

    if (should_yield) {
        fr_port_yield();
    }
}