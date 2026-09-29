#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/critical.h"
#include "forge/event_flags.h"
#include "forge/kernel/scheduler_internal.h"
#include "forge/kernel/task_internal.h"
#include "forge/kernel/wait_internal.h"
#include "forge/scheduler.h"

#define FR_EVENT_FLAGS_MAGIC 0x46524546u

static bool fr_event_flags_thread_context_allowed(void) {
    uint32_t ipsr;
    uint32_t primask;
    uint32_t faultmask;
    uint32_t basepri;

    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __asm volatile("mrs %0, faultmask" : "=r"(faultmask));
    __asm volatile("mrs %0, basepri" : "=r"(basepri));

    return (ipsr == 0u) &&
           ((primask & 1u) == 0u) &&
           ((faultmask & 1u) == 0u) &&
           (basepri == 0u);
}

static bool fr_event_flags_timeout_valid(uint32_t timeout_ticks) {
    return (timeout_ticks <= FR_WAIT_MAX_FINITE_TICKS) ||
           (timeout_ticks == FR_WAIT_FOREVER);
}

static bool fr_event_flags_condition_met(uint32_t current_bits,
                                         uint32_t mask,
                                         bool wait_all,
                                         uint32_t *matched_bits) {
    const uint32_t matched = current_bits & mask;

    if (matched_bits != NULL) {
        *matched_bits = matched;
    }

    if (wait_all) {
        return matched == mask;
    }

    return matched != 0u;
}

bool fr_event_flags_init(fr_event_flags_t *flags) {
    if ((flags == NULL) || fr_scheduler_is_running()) {
        return false;
    }

    flags->magic = 0u;
    flags->bits = 0u;
    flags->magic = FR_EVENT_FLAGS_MAGIC;

    return true;
}

bool fr_event_flags_set(fr_event_flags_t *flags, uint32_t bits) {
    if ((flags == NULL) ||
        (bits == 0u) ||
        !fr_event_flags_thread_context_allowed()) {
        return false;
    }

    const fr_critical_state_t critical_state =
        fr_critical_enter();

    if ((critical_state != 0u) ||
        (flags->magic != FR_EVENT_FLAGS_MAGIC) ||
        !fr_scheduler_is_running() ||
        (fr_scheduler_current_task() == NULL)) {
        fr_critical_exit(critical_state);
        return false;
    }

    flags->bits |= bits;

    /*
     * Every waiter must observe the same pre-clear snapshot.
     */
    const uint32_t current_bits = flags->bits;
    uint32_t clear_mask = 0u;
    bool task_woken = false;

    const uint32_t task_count = fr_task_internal_count();

    for (uint32_t i = 0u; i < task_count; ++i) {
        fr_task_t *const waiter = fr_task_internal_at(i);

        if ((waiter == NULL) ||
            (waiter->state != FR_TASK_STATE_BLOCKED) ||
            (waiter->wait_reason != FR_WAIT_REASON_EVENT_FLAGS) ||
            (waiter->wait_result != FR_WAIT_RESULT_PENDING) ||
            (waiter->wait_object != flags) ||
            (waiter->wait_flags_mask == 0u)) {
            continue;
        }

        uint32_t matched = 0u;

        if (!fr_event_flags_condition_met(
                current_bits,
                waiter->wait_flags_mask,
                waiter->wait_flags_all,
                &matched)) {
            continue;
        }

        const uint32_t waiter_mask =
            waiter->wait_flags_mask;

        const bool clear_on_exit =
            waiter->wait_flags_clear_on_exit;

        waiter->wait_flags_result = matched;

        if (fr_scheduler_unblock_task_locked(
                waiter,
                FR_WAIT_RESULT_SIGNALED)) {
            task_woken = true;

            if (clear_on_exit) {
                clear_mask |= waiter_mask;
            }
        }
    }

    flags->bits &= ~clear_mask;

    if (task_woken) {
        fr_scheduler_request_if_needed_locked();
    }

    fr_critical_exit(critical_state);
    return true;
}

bool fr_event_flags_clear(fr_event_flags_t *flags,
                          uint32_t bits) {
    if ((flags == NULL) ||
        (bits == 0u) ||
        !fr_event_flags_thread_context_allowed()) {
        return false;
    }

    const fr_critical_state_t critical_state =
        fr_critical_enter();

    if ((critical_state != 0u) ||
        (flags->magic != FR_EVENT_FLAGS_MAGIC) ||
        !fr_scheduler_is_running() ||
        (fr_scheduler_current_task() == NULL)) {
        fr_critical_exit(critical_state);
        return false;
    }

    flags->bits &= ~bits;

    fr_critical_exit(critical_state);
    return true;
}

bool fr_event_flags_get(fr_event_flags_t *flags,
                        uint32_t *bits) {
    if ((flags == NULL) ||
        (bits == NULL) ||
        !fr_event_flags_thread_context_allowed()) {
        return false;
    }

    const fr_critical_state_t critical_state =
        fr_critical_enter();

    if ((critical_state != 0u) ||
        (flags->magic != FR_EVENT_FLAGS_MAGIC) ||
        !fr_scheduler_is_running() ||
        (fr_scheduler_current_task() == NULL)) {
        fr_critical_exit(critical_state);
        return false;
    }

    *bits = flags->bits;

    fr_critical_exit(critical_state);
    return true;
}

bool fr_event_flags_wait(fr_event_flags_t *flags,
                         uint32_t mask,
                         bool wait_all,
                         bool clear_on_exit,
                         uint32_t timeout_ticks,
                         uint32_t *matched_bits) {
    if (matched_bits != NULL) {
        *matched_bits = 0u;
    }

    if ((flags == NULL) ||
        (mask == 0u) ||
        !fr_event_flags_thread_context_allowed() ||
        !fr_event_flags_timeout_valid(timeout_ticks)) {
        return false;
    }

    const fr_critical_state_t critical_state =
        fr_critical_enter();

    fr_task_t *const current_task =
        fr_scheduler_current_task();

    if ((critical_state != 0u) ||
        (flags->magic != FR_EVENT_FLAGS_MAGIC) ||
        !fr_scheduler_is_running() ||
        (current_task == NULL)) {
        fr_critical_exit(critical_state);
        return false;
    }

    uint32_t matched = 0u;

    if (fr_event_flags_condition_met(flags->bits,
                                     mask,
                                     wait_all,
                                     &matched)) {
        if (clear_on_exit) {
            flags->bits &= ~mask;
        }

        if (matched_bits != NULL) {
            *matched_bits = matched;
        }

        fr_critical_exit(critical_state);
        return true;
    }

    if (timeout_ticks == 0u) {
        fr_critical_exit(critical_state);
        return false;
    }

    current_task->wait_flags_mask = mask;
    current_task->wait_flags_result = 0u;
    current_task->wait_flags_all = wait_all;
    current_task->wait_flags_clear_on_exit =
        clear_on_exit;

    const bool blocked =
        fr_scheduler_block_current_locked(
            FR_WAIT_REASON_EVENT_FLAGS,
            flags,
            timeout_ticks);

    if (!blocked) {
        current_task->wait_flags_mask = 0u;
        current_task->wait_flags_all = false;
        current_task->wait_flags_clear_on_exit = false;
    }

    fr_critical_exit(critical_state);

    if (!blocked ||
        (current_task->wait_result !=
         FR_WAIT_RESULT_SIGNALED)) {
        return false;
    }

    if (matched_bits != NULL) {
        *matched_bits =
            current_task->wait_flags_result;
    }

    return true;
}