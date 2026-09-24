#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "forge/critical.h"
#include "forge/kernel/scheduler_internal.h"
#include "forge/kernel/task_internal.h"
#include "forge/kernel/wait_internal.h"
#include "forge/scheduler.h"
#include "forge/semaphore.h"

#define FR_BINARY_SEMAPHORE_MAGIC   0x46525342u
#define FR_COUNTING_SEMAPHORE_MAGIC 0x46524353u

static bool fr_semaphore_thread_context_allowed(void) {
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

static bool fr_semaphore_timeout_valid(uint32_t timeout_ticks) {
    return (timeout_ticks <= FR_WAIT_MAX_FINITE_TICKS) ||
           (timeout_ticks == FR_WAIT_FOREVER);
}

static bool fr_semaphore_take_common(uint32_t *count,
                                     const void *wait_object,
                                     uint32_t timeout_ticks) {
    if ((count == NULL) ||
        (wait_object == NULL) ||
        !fr_semaphore_thread_context_allowed() ||
        !fr_semaphore_timeout_valid(timeout_ticks)) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();
    fr_task_t *const current_task = fr_scheduler_current_task();

    if ((critical_state != 0u) ||
        !fr_scheduler_is_running() ||
        (current_task == NULL)) {
        fr_critical_exit(critical_state);
        return false;
    }

    if (*count != 0u) {
        --(*count);
        fr_critical_exit(critical_state);
        return true;
    }

    if (timeout_ticks == 0u) {
        fr_critical_exit(critical_state);
        return false;
    }

    const bool blocked =
        fr_scheduler_block_current_locked(FR_WAIT_REASON_SYNC,
                                          wait_object,
                                          timeout_ticks);

    fr_critical_exit(critical_state);

    return blocked &&
           (current_task->wait_result == FR_WAIT_RESULT_SIGNALED);
}

static bool fr_semaphore_give_common(uint32_t *count,
                                     uint32_t max_count,
                                     const void *wait_object) {
    if ((count == NULL) ||
        (max_count == 0u) ||
        (wait_object == NULL) ||
        !fr_semaphore_thread_context_allowed()) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();
    fr_task_t *const current_task = fr_scheduler_current_task();

    if ((critical_state != 0u) ||
        !fr_scheduler_is_running() ||
        (current_task == NULL)) {
        fr_critical_exit(critical_state);
        return false;
    }

    fr_task_t *const waiter =
        fr_scheduler_select_waiter_locked(FR_WAIT_REASON_SYNC, wait_object);

    if (waiter != NULL) {
        /*
         * A blocked waiter and an already available token should not
         * coexist. Treat this as an invalid kernel state rather than
         * duplicating a token.
         */
        if (*count != 0u) {
            fr_critical_exit(critical_state);
            return false;
        }

        if (!fr_scheduler_unblock_task_locked(
                waiter,
                FR_WAIT_RESULT_SIGNALED)) {
            fr_critical_exit(critical_state);
            return false;
        }

        fr_scheduler_request_if_needed_locked();

        fr_critical_exit(critical_state);
        return true;
    }

    if (*count >= max_count) {
        fr_critical_exit(critical_state);
        return false;
    }

    ++(*count);

    fr_critical_exit(critical_state);
    return true;
}

bool fr_binary_semaphore_init(fr_binary_semaphore_t *semaphore,
                              bool initially_available) {
    if ((semaphore == NULL) || fr_scheduler_is_running()) {
        return false;
    }

    semaphore->magic = 0u;
    semaphore->available = initially_available ? 1u : 0u;
    semaphore->magic = FR_BINARY_SEMAPHORE_MAGIC;

    return true;
}

bool fr_binary_semaphore_take(fr_binary_semaphore_t *semaphore,
                              uint32_t timeout_ticks) {
    if ((semaphore == NULL) ||
        (semaphore->magic != FR_BINARY_SEMAPHORE_MAGIC) ||
        (semaphore->available > 1u)) {
        return false;
    }

    return fr_semaphore_take_common(&semaphore->available,
                                    semaphore,
                                    timeout_ticks);
}

bool fr_binary_semaphore_give(fr_binary_semaphore_t *semaphore) {
    if ((semaphore == NULL) ||
        (semaphore->magic != FR_BINARY_SEMAPHORE_MAGIC) ||
        (semaphore->available > 1u)) {
        return false;
    }

    return fr_semaphore_give_common(&semaphore->available,
                                    1u,
                                    semaphore);
}

bool fr_counting_semaphore_init(fr_counting_semaphore_t *semaphore,
                                uint32_t initial_count,
                                uint32_t max_count) {
    if ((semaphore == NULL) ||
        (max_count == 0u) ||
        (initial_count > max_count) ||
        fr_scheduler_is_running()) {
        return false;
    }

    semaphore->magic = 0u;
    semaphore->count = initial_count;
    semaphore->max_count = max_count;
    semaphore->magic = FR_COUNTING_SEMAPHORE_MAGIC;

    return true;
}

bool fr_counting_semaphore_take(fr_counting_semaphore_t *semaphore,
                                uint32_t timeout_ticks) {
    if ((semaphore == NULL) ||
        (semaphore->magic != FR_COUNTING_SEMAPHORE_MAGIC) ||
        (semaphore->max_count == 0u) ||
        (semaphore->count > semaphore->max_count)) {
        return false;
    }

    return fr_semaphore_take_common(&semaphore->count,
                                    semaphore,
                                    timeout_ticks);
}

bool fr_counting_semaphore_give(fr_counting_semaphore_t *semaphore) {
    if ((semaphore == NULL) ||
        (semaphore->magic != FR_COUNTING_SEMAPHORE_MAGIC) ||
        (semaphore->max_count == 0u) ||
        (semaphore->count > semaphore->max_count)) {
        return false;
    }

    return fr_semaphore_give_common(&semaphore->count,
                                    semaphore->max_count,
                                    semaphore);
}