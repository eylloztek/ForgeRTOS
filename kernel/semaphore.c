#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "forge/critical.h"
#include "forge/kernel/port.h"
#include "forge/kernel/scheduler_internal.h"
#include "forge/kernel/task_internal.h"
#include "forge/kernel/wait_internal.h"
#include "forge/scheduler.h"
#include "forge/semaphore.h"

#define FR_BINARY_SEMAPHORE_MAGIC 0x46525342u

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
        !fr_semaphore_thread_context_allowed() ||
        (timeout_ticks > FR_WAIT_MAX_FINITE_TICKS &&
         timeout_ticks != FR_BINARY_SEMAPHORE_WAIT_FOREVER)) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();

    if ((critical_state != 0u) ||
        (semaphore->magic != FR_BINARY_SEMAPHORE_MAGIC) ||
        !fr_scheduler_is_running()) {
        fr_critical_exit(critical_state);
        return false;
    }

    if (semaphore->available != 0u) {
        semaphore->available = 0u;
        fr_critical_exit(critical_state);
        return true;
    }

    if (timeout_ticks == 0u) {
        fr_critical_exit(critical_state);
        return false;
    }

    fr_task_t *const current_task = fr_scheduler_current_task();

    const bool blocked =
        fr_scheduler_block_current_locked(FR_WAIT_REASON_SYNC,
                                          semaphore,
                                          timeout_ticks);

    fr_critical_exit(critical_state);

    return blocked &&
           (current_task != NULL) &&
           (current_task->wait_result == FR_WAIT_RESULT_SIGNALED);
}

bool fr_binary_semaphore_give(fr_binary_semaphore_t *semaphore) {
    if ((semaphore == NULL) || !fr_semaphore_thread_context_allowed()) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();

    if ((critical_state != 0u) ||
        (semaphore->magic != FR_BINARY_SEMAPHORE_MAGIC) ||
        !fr_scheduler_is_running()) {
        fr_critical_exit(critical_state);
        return false;
    }

    if (semaphore->available != 0u) {
        fr_critical_exit(critical_state);
        return false;
    }

    fr_task_t *const waiter = fr_scheduler_select_waiter_locked(semaphore);

    if (waiter != NULL) {
        if (!fr_scheduler_unblock_task_locked(waiter, FR_WAIT_RESULT_SIGNALED)) {
            fr_critical_exit(critical_state);
            return false;
        }

        fr_scheduler_request_if_needed_locked();
    } else {
        semaphore->available = 1u;
    }

    fr_critical_exit(critical_state);
    return true;
}