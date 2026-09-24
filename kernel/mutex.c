#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "forge/critical.h"
#include "forge/kernel/scheduler_internal.h"
#include "forge/kernel/task_internal.h"
#include "forge/kernel/wait_internal.h"
#include "forge/mutex.h"
#include "forge/scheduler.h"

#define FR_MUTEX_MAGIC 0x46524D58u

static bool fr_mutex_thread_context_allowed(void) {
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

static bool fr_mutex_timeout_valid(uint32_t timeout_ticks) {
    return (timeout_ticks <= FR_WAIT_MAX_FINITE_TICKS) ||
           (timeout_ticks == FR_WAIT_FOREVER);
}

bool fr_mutex_init(fr_mutex_t *mutex) {
    if ((mutex == NULL) || fr_scheduler_is_running()) {
        return false;
    }

    mutex->magic = 0u;
    mutex->owner = NULL;
    mutex->magic = FR_MUTEX_MAGIC;

    return true;
}

bool fr_mutex_lock(fr_mutex_t *mutex, uint32_t timeout_ticks) {
    if ((mutex == NULL) ||
        !fr_mutex_thread_context_allowed() ||
        !fr_mutex_timeout_valid(timeout_ticks)) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();
    fr_task_t *const current_task = fr_scheduler_current_task();

    if ((critical_state != 0u) ||
        (mutex->magic != FR_MUTEX_MAGIC) ||
        !fr_scheduler_is_running() ||
        (current_task == NULL)) {
        fr_critical_exit(critical_state);
        return false;
    }

    if (mutex->owner == NULL) {
        mutex->owner = current_task;

        fr_critical_exit(critical_state);
        return true;
    }

    /*
     * ForgeRTOS mutexes are intentionally non-recursive.
     */
    if (mutex->owner == current_task) {
        fr_critical_exit(critical_state);
        return false;
    }

    if (timeout_ticks == 0u) {
        fr_critical_exit(critical_state);
        return false;
    }

    const bool blocked =
        fr_scheduler_block_current_locked(FR_WAIT_REASON_MUTEX,
                                          mutex,
                                          timeout_ticks);

    fr_critical_exit(critical_state);

    return blocked &&
           (current_task->wait_result == FR_WAIT_RESULT_SIGNALED) &&
           (mutex->owner == current_task);
}

bool fr_mutex_unlock(fr_mutex_t *mutex) {
    if ((mutex == NULL) || !fr_mutex_thread_context_allowed()) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();
    fr_task_t *const current_task = fr_scheduler_current_task();

    if ((critical_state != 0u) ||
        (mutex->magic != FR_MUTEX_MAGIC) ||
        !fr_scheduler_is_running() ||
        (current_task == NULL) ||
        (mutex->owner != current_task)) {
        fr_critical_exit(critical_state);
        return false;
    }

    fr_task_t *const waiter =
        fr_scheduler_select_waiter_locked(FR_WAIT_REASON_MUTEX,
                                          mutex);

    if (waiter == NULL) {
        mutex->owner = NULL;

        fr_critical_exit(critical_state);
        return true;
    }

    /*
     * Direct ownership handoff:
     * the mutex never becomes momentarily ownerless.
     */
    mutex->owner = waiter;

    if (!fr_scheduler_unblock_task_locked(
            waiter,
            FR_WAIT_RESULT_SIGNALED)) {
        mutex->owner = current_task;

        fr_critical_exit(critical_state);
        return false;
    }

    fr_scheduler_request_if_needed_locked();

    fr_critical_exit(critical_state);
    return true;
}