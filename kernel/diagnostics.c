#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/config.h"
#include "forge/critical.h"
#include "forge/kernel/task_internal.h"
#include "forge/kernel/wait_internal.h"
#include "forge/kernel_diagnostics.h"
#include "forge/scheduler.h"
#include "forge/task.h"

static void fr_kernel_note_violation(fr_kernel_invariant_report_t *report,
                                     uint32_t violation,
                                     uint32_t task_id) {
    report->violation_mask |= violation;

    if (report->first_bad_task_id == FR_KERNEL_INVARIANT_NO_TASK_ID) {
        report->first_bad_task_id = task_id;
    }
}

static bool fr_kernel_wait_reason_uses_object(fr_wait_reason_t reason) {
    return (reason == FR_WAIT_REASON_SYNC) ||
           (reason == FR_WAIT_REASON_MUTEX) ||
           (reason == FR_WAIT_REASON_QUEUE_SEND) ||
           (reason == FR_WAIT_REASON_QUEUE_RECEIVE) ||
           (reason == FR_WAIT_REASON_EVENT_FLAGS);
}

static bool fr_kernel_wait_state_valid(const fr_task_t *task) {
    if (task->state == FR_TASK_STATE_BLOCKED) {
        if ((task->wait_reason == FR_WAIT_REASON_NONE) ||
            (task->wait_result != FR_WAIT_RESULT_PENDING)) {
            return false;
        }

        if (task->wait_reason == FR_WAIT_REASON_SLEEP) {
            if (task->wait_object != NULL) {
                return false;
            }
        } else if (fr_kernel_wait_reason_uses_object(task->wait_reason)) {
            if (task->wait_object == NULL) {
                return false;
            }
        } else {
            return false;
        }

        if (task->wait_reason == FR_WAIT_REASON_EVENT_FLAGS) {
            if (task->wait_flags_mask == 0u) {
                return false;
            }
        } else if ((task->wait_flags_mask != 0u) ||
                   task->wait_flags_all ||
                   task->wait_flags_clear_on_exit) {
            return false;
        }

        return true;
    }

    return (task->wait_reason == FR_WAIT_REASON_NONE) &&
           (task->wait_object == NULL) &&
           !task->wait_has_deadline &&
           (task->wait_deadline == 0u) &&
           (task->wait_flags_mask == 0u) &&
           !task->wait_flags_all &&
           !task->wait_flags_clear_on_exit;
}

static bool fr_kernel_stack_geometry_valid(const fr_task_t *task) {
    if ((task->stack_base == NULL) ||
        (task->stack_top == NULL) ||
        (task->stack_size_words < FR_CONFIG_MIN_TASK_STACK_WORDS) ||
        ((task->stack_size_words & 1u) != 0u)) {
        return false;
    }

    if (task->stack_size_words >
        (uint32_t)(UINTPTR_MAX / sizeof(uint32_t))) {
        return false;
    }

    const uintptr_t stack_base = (uintptr_t)task->stack_base;
    const uintptr_t stack_top = (uintptr_t)task->stack_top;
    const uintptr_t stack_size_bytes =
        (uintptr_t)task->stack_size_words * sizeof(uint32_t);

    if (((stack_base & 0x7u) != 0u) ||
        ((stack_top & 0x7u) != 0u) ||
        (stack_base > (UINTPTR_MAX - stack_size_bytes))) {
        return false;
    }

    return stack_top == (stack_base + stack_size_bytes);
}

static bool fr_kernel_saved_sp_valid(const fr_task_t *task) {
    if (task->saved_sp == NULL) {
        return false;
    }

    const uintptr_t saved_sp = (uintptr_t)task->saved_sp;
    const uintptr_t stack_base = (uintptr_t)task->stack_base;
    const uintptr_t stack_top = (uintptr_t)task->stack_top;

    return (saved_sp >= stack_base) &&
           (saved_sp <= stack_top) &&
           ((saved_sp & 0x7u) == 0u);
}

bool fr_kernel_check_invariants(fr_kernel_invariant_report_t *out_report) {
    if (out_report == NULL) {
        return false;
    }

    out_report->task_count = 0u;
    out_report->created_tasks = 0u;
    out_report->ready_tasks = 0u;
    out_report->running_tasks = 0u;
    out_report->blocked_tasks = 0u;
    out_report->suspended_tasks = 0u;
    out_report->current_task_id = 0u;
    out_report->violation_mask = 0u;
    out_report->first_bad_task_id = FR_KERNEL_INVARIANT_NO_TASK_ID;
    out_report->scheduler_running = false;

    const fr_critical_state_t critical_state = fr_critical_enter();

    const uint32_t task_count = fr_task_internal_count();
    const bool scheduler_running = fr_scheduler_is_running();
    fr_task_handle_t const current_task = fr_scheduler_current_task();

    out_report->task_count = task_count;
    out_report->scheduler_running = scheduler_running;

    uint32_t scan_count = task_count;

    if (task_count > FR_CONFIG_MAX_TASKS) {
        fr_kernel_note_violation(out_report,
                                 FR_KERNEL_INVARIANT_TASK_COUNT,
                                 FR_KERNEL_INVARIANT_NO_TASK_ID);
        scan_count = FR_CONFIG_MAX_TASKS;
    }

    bool current_task_found = false;

    for (uint32_t i = 0u; i < scan_count; ++i) {
        fr_task_t *const task = fr_task_internal_at(i);
        const uint32_t expected_id = i + 1u;

        if (task == NULL) {
            fr_kernel_note_violation(out_report,
                                     FR_KERNEL_INVARIANT_TASK_POINTER,
                                     expected_id);
            continue;
        }

        if (task == current_task) {
            current_task_found = true;
            out_report->current_task_id = task->id;
        }

        if (task->id != expected_id) {
            fr_kernel_note_violation(out_report,
                                     FR_KERNEL_INVARIANT_TASK_ID,
                                     task->id);
        }

        if ((task->priority > FR_CONFIG_MAX_TASK_PRIORITY) ||
            (task->base_priority > FR_CONFIG_MAX_TASK_PRIORITY) ||
            (task->priority < task->base_priority)) {
            fr_kernel_note_violation(out_report,
                                     FR_KERNEL_INVARIANT_TASK_PRIORITY,
                                     task->id);
        }

        if (!fr_kernel_stack_geometry_valid(task)) {
            fr_kernel_note_violation(out_report,
                                     FR_KERNEL_INVARIANT_STACK_GEOMETRY,
                                     task->id);
        } else if (!fr_kernel_saved_sp_valid(task)) {
            fr_kernel_note_violation(out_report,
                                     FR_KERNEL_INVARIANT_SAVED_SP,
                                     task->id);
        }

        if (!fr_kernel_wait_state_valid(task)) {
            fr_kernel_note_violation(out_report,
                                     FR_KERNEL_INVARIANT_WAIT_STATE,
                                     task->id);
        }

        switch (task->state) {
            case FR_TASK_STATE_CREATED:
                ++out_report->created_tasks;

                if (scheduler_running) {
                    fr_kernel_note_violation(out_report,
                                             FR_KERNEL_INVARIANT_TASK_STATE,
                                             task->id);
                }
                break;

            case FR_TASK_STATE_READY:
                ++out_report->ready_tasks;
                break;

            case FR_TASK_STATE_RUNNING:
                ++out_report->running_tasks;
                break;

            case FR_TASK_STATE_BLOCKED:
                ++out_report->blocked_tasks;
                break;

            case FR_TASK_STATE_SUSPENDED:
                ++out_report->suspended_tasks;
                break;

            default:
                fr_kernel_note_violation(out_report,
                                         FR_KERNEL_INVARIANT_TASK_STATE,
                                         task->id);
                break;
        }
    }

    if (out_report->running_tasks > 1u) {
        fr_kernel_note_violation(out_report,
                                 FR_KERNEL_INVARIANT_RUNNING_COUNT,
                                 FR_KERNEL_INVARIANT_NO_TASK_ID);
    }

    if (scheduler_running) {
        if (current_task != NULL) {
            if (!current_task_found ||
                (current_task->state != FR_TASK_STATE_RUNNING) ||
                (out_report->running_tasks != 1u)) {
                fr_kernel_note_violation(out_report,
                                         FR_KERNEL_INVARIANT_CURRENT_TASK,
                                         out_report->current_task_id);
            }
        } else if (out_report->running_tasks != 0u) {
            /* Public current-task is NULL while the kernel-owned idle task runs. */
            fr_kernel_note_violation(out_report,
                                     FR_KERNEL_INVARIANT_CURRENT_TASK,
                                     FR_KERNEL_INVARIANT_NO_TASK_ID);
        }
    } else if (current_task != NULL) {
        fr_kernel_note_violation(out_report,
                                 FR_KERNEL_INVARIANT_CURRENT_TASK,
                                 out_report->current_task_id);
    }

    const bool valid = out_report->violation_mask == 0u;

    fr_critical_exit(critical_state);
    return valid;
}
