#include <stddef.h>

#include "forge/critical.h"
#include "forge/kernel/port.h"
#include "forge/kernel/scheduler_internal.h"
#include "forge/kernel/task_internal.h"
#include "forge/scheduler.h"
#include "forge/task.h"

static fr_task_t *volatile g_fr_scheduler_current_task;
static volatile bool g_fr_scheduler_running;

static fr_task_t *fr_scheduler_select_highest_ready(const fr_task_t *excluded_task) {
    fr_task_t *selected = NULL;

    const uint32_t task_count = fr_task_internal_count();

    for (uint32_t i = 0u; i < task_count; ++i) {
        fr_task_t *const task = fr_task_internal_at(i);

        if ((task == NULL) ||
            (task == excluded_task) ||
            (task->state != FR_TASK_STATE_READY)) {
            continue;
        }

        if ((selected == NULL) || (task->priority > selected->priority)) {
            selected = task;
        }
    }

    return selected;
}

fr_scheduler_status_t fr_scheduler_start(void) {
    const fr_critical_state_t critical_state = fr_critical_enter();

    if (g_fr_scheduler_running) {
        fr_critical_exit(critical_state);
        return FR_SCHEDULER_ERROR_ALREADY_RUNNING;
    }

    fr_task_t *const first_task = fr_scheduler_select_highest_ready(NULL);

    if (first_task == NULL) {
        fr_critical_exit(critical_state);
        return FR_SCHEDULER_ERROR_NO_READY_TASK;
    }

    first_task->state = FR_TASK_STATE_RUNNING;

    g_fr_scheduler_current_task = first_task;
    g_fr_scheduler_running = true;

    fr_critical_exit(critical_state);

    fr_port_start_first_task();
}

bool fr_scheduler_is_running(void) {
    return g_fr_scheduler_running;
}

fr_task_handle_t fr_scheduler_current_task(void) {
    return g_fr_scheduler_current_task;
}

uint32_t *fr_scheduler_current_saved_sp(void) {
    fr_task_t *const current_task = g_fr_scheduler_current_task;

    if (current_task == NULL) {
        return NULL;
    }

    return current_task->saved_sp;
}

uint32_t *fr_scheduler_yield_from_isr(uint32_t *current_saved_sp) {
    if (current_saved_sp == NULL) {
        return NULL;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();

    fr_task_t *const current_task = g_fr_scheduler_current_task;

    if ((!g_fr_scheduler_running) ||
        (current_task == NULL) ||
        (current_task->state != FR_TASK_STATE_RUNNING)) {
        fr_critical_exit(critical_state);
        return NULL;
    }

    current_task->saved_sp = current_saved_sp;
    current_task->state = FR_TASK_STATE_READY;

    fr_task_t *next_task = fr_scheduler_select_highest_ready(current_task);

    if (next_task == NULL) {
        next_task = current_task;
    }

    next_task->state = FR_TASK_STATE_RUNNING;
    g_fr_scheduler_current_task = next_task;

    uint32_t *const next_saved_sp = next_task->saved_sp;

    fr_critical_exit(critical_state);

    return next_saved_sp;
}

void fr_task_yield(void) {
    if ((!g_fr_scheduler_running) || (g_fr_scheduler_current_task == NULL)) {
        return;
    }

    fr_port_yield();
}