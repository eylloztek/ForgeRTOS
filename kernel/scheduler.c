#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/config.h"
#include "forge/tick.h"
#include "forge/kernel/wait_internal.h"
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

volatile uint32_t g_fr_scheduler_timeout_count;
volatile uint32_t g_fr_scheduler_last_timeout_task_id;
volatile uint32_t g_fr_scheduler_last_timeout_tick;
volatile uint32_t g_fr_scheduler_last_timeout_deadline;

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

    idle->wait_deadline = 0u;
    idle->wait_object = NULL;
    idle->wait_reason = FR_WAIT_REASON_NONE;
    idle->wait_result = FR_WAIT_RESULT_PENDING;
    idle->wait_has_deadline = false;

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

    g_fr_scheduler_timeout_count = 0u;
    g_fr_scheduler_last_timeout_task_id = 0u;
    g_fr_scheduler_last_timeout_tick = 0u;
    g_fr_scheduler_last_timeout_deadline = 0u;

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

static void fr_scheduler_expire_timeouts(uint32_t now) {
    const uint32_t task_count = fr_task_internal_count();

    for (uint32_t i = 0u; i < task_count; ++i) {
        fr_task_t *const task = fr_task_internal_at(i);

        if ((task == NULL) ||
            (task->state != FR_TASK_STATE_BLOCKED) ||
            !task->wait_has_deadline) {
            continue;
        }

        if (!fr_tick_deadline_reached(now, task->wait_deadline)) {
            continue;
        }

        const uint32_t deadline = task->wait_deadline;

        if (fr_scheduler_unblock_task_locked(task, FR_WAIT_RESULT_TIMEOUT)) {
            ++g_fr_scheduler_timeout_count;
            g_fr_scheduler_last_timeout_task_id = task->id;
            g_fr_scheduler_last_timeout_tick = now;
            g_fr_scheduler_last_timeout_deadline = deadline;
        }
    }
}

void fr_scheduler_tick_isr(void) {
    if (!g_fr_scheduler_running) {
        return;
    }

    fr_scheduler_expire_timeouts(fr_tick_now());

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

bool fr_scheduler_block_current_locked(fr_wait_reason_t reason,
                                       const void *wait_object,
                                       uint32_t timeout_ticks) {
    if (((reason != FR_WAIT_REASON_SLEEP) &&
         (reason != FR_WAIT_REASON_SYNC)) ||
        ((reason == FR_WAIT_REASON_SLEEP) && (wait_object != NULL)) ||
        ((reason == FR_WAIT_REASON_SYNC) && (wait_object == NULL)) ||
        (timeout_ticks == 0u) ||
        ((timeout_ticks > FR_WAIT_MAX_FINITE_TICKS) &&
         (timeout_ticks != FR_WAIT_FOREVER))) {
        return false;
    }

    fr_task_t *const current_task = g_fr_scheduler_current_task;

    if (!g_fr_scheduler_running ||
        (current_task == NULL) ||
        (current_task == &g_fr_scheduler_idle_task) ||
        (current_task->state != FR_TASK_STATE_RUNNING)) {
        return false;
    }

    current_task->wait_object = wait_object;
    current_task->wait_reason = reason;
    current_task->wait_result = FR_WAIT_RESULT_PENDING;

    if (timeout_ticks == FR_WAIT_FOREVER) {
        current_task->wait_has_deadline = false;
        current_task->wait_deadline = 0u;
    } else {
        current_task->wait_has_deadline = true;
        current_task->wait_deadline = fr_tick_now() + timeout_ticks;
    }

    current_task->state = FR_TASK_STATE_BLOCKED;
    fr_port_request_context_switch();

    return true;
}

bool fr_scheduler_unblock_task_locked(fr_task_t *task,
                                     fr_wait_result_t result) {
    if ((task == NULL) ||
        (task->state != FR_TASK_STATE_BLOCKED) ||
        (task->wait_reason == FR_WAIT_REASON_NONE) ||
        ((result != FR_WAIT_RESULT_TIMEOUT) &&
         (result != FR_WAIT_RESULT_SIGNALED))) {
        return false;
    }

    task->wait_result = result;
    task->wait_reason = FR_WAIT_REASON_NONE;
    task->wait_object = NULL;
    task->wait_has_deadline = false;
    task->wait_deadline = 0u;

    if (task == g_fr_scheduler_current_task) {
        task->state = FR_TASK_STATE_RUNNING;
    } else {
        task->state = FR_TASK_STATE_READY;
    }

    return true;
}

fr_task_t *fr_scheduler_select_waiter_locked(const void *wait_object) {
    if (wait_object == NULL) {
        return NULL;
    }

    fr_task_t *selected = NULL;
    const uint32_t task_count = fr_task_internal_count();

    for (uint32_t i = 0u; i < task_count; ++i) {
        fr_task_t *const task = fr_task_internal_at(i);

        if ((task == NULL) ||
            (task->state != FR_TASK_STATE_BLOCKED) ||
            (task->wait_reason != FR_WAIT_REASON_SYNC) ||
            (task->wait_result != FR_WAIT_RESULT_PENDING) ||
            (task->wait_object != wait_object)) {
            continue;
        }

        if ((selected == NULL) || (task->priority > selected->priority)) {
            selected = task;
        }
    }

    return selected;
}

void fr_scheduler_request_if_needed_locked(void) {
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

bool fr_task_sleep(uint32_t delay_ticks) {
    if ((delay_ticks == 0u) || (delay_ticks > FR_TASK_SLEEP_MAX_TICKS)) {
        return false;
    }

    uint32_t ipsr;
    uint32_t primask;
    uint32_t faultmask;

    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __asm volatile("mrs %0, faultmask" : "=r"(faultmask));

    if ((ipsr != 0u) ||
        ((primask & 1u) != 0u) ||
        ((faultmask & 1u) != 0u)) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();
    fr_task_t *const current_task = g_fr_scheduler_current_task;

    if ((critical_state != 0u) ||
        !g_fr_scheduler_running ||
        (current_task == NULL) ||
        (current_task == &g_fr_scheduler_idle_task) ||
        (current_task->state != FR_TASK_STATE_RUNNING)) {
        fr_critical_exit(critical_state);
        return false;
    }

    const bool blocked =
        fr_scheduler_block_current_locked(FR_WAIT_REASON_SLEEP, NULL, delay_ticks);

    fr_critical_exit(critical_state);

    return blocked &&
           (current_task->wait_result == FR_WAIT_RESULT_TIMEOUT);
}
