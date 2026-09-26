#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/critical.h"
#include "forge/kernel/mutex_internal.h"
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

static fr_task_priority_t fr_mutex_compute_effective_priority_locked(
    const fr_task_t *owner) {
    fr_task_priority_t effective_priority = owner->base_priority;
    const uint32_t task_count = fr_task_internal_count();

    for (uint32_t i = 0u; i < task_count; ++i) {
        const fr_task_t *const waiter = fr_task_internal_at(i);

        if ((waiter == NULL) ||
            (waiter->state != FR_TASK_STATE_BLOCKED) ||
            (waiter->wait_reason != FR_WAIT_REASON_MUTEX) ||
            (waiter->wait_result != FR_WAIT_RESULT_PENDING) ||
            (waiter->wait_object == NULL)) {
            continue;
        }

        const fr_mutex_t *const mutex =
            (const fr_mutex_t *)waiter->wait_object;

        if ((mutex->magic != FR_MUTEX_MAGIC) ||
            (mutex->owner != owner)) {
            continue;
        }

        if (waiter->priority > effective_priority) {
            effective_priority = waiter->priority;
        }
    }

    return effective_priority;
}

static void fr_mutex_inherit_priority_chain_locked(
    fr_task_t *owner,
    fr_task_priority_t donated_priority) {
    const uint32_t task_count = fr_task_internal_count();

    for (uint32_t depth = 0u;
         (owner != NULL) && (depth < task_count);
         ++depth) {
        if (donated_priority > owner->priority) {
            owner->priority = donated_priority;
        }

        donated_priority = owner->priority;

        if ((owner->state != FR_TASK_STATE_BLOCKED) ||
            (owner->wait_reason != FR_WAIT_REASON_MUTEX) ||
            (owner->wait_object == NULL)) {
            break;
        }

        fr_mutex_t *const upstream_mutex =
            (fr_mutex_t *)owner->wait_object;

        if (upstream_mutex->magic != FR_MUTEX_MAGIC) {
            break;
        }

        fr_task_t *const upstream_owner = upstream_mutex->owner;

        if ((upstream_owner == NULL) ||
            (upstream_owner == owner)) {
            break;
        }

        owner = upstream_owner;
    }
}

static void fr_mutex_recompute_priority_chain_locked(fr_task_t *task) {
    const uint32_t task_count = fr_task_internal_count();

    for (uint32_t depth = 0u;
         (task != NULL) && (depth < task_count);
         ++depth) {
        task->priority =
            fr_mutex_compute_effective_priority_locked(task);

        if ((task->state != FR_TASK_STATE_BLOCKED) ||
            (task->wait_reason != FR_WAIT_REASON_MUTEX) ||
            (task->wait_object == NULL)) {
            break;
        }

        fr_mutex_t *const upstream_mutex =
            (fr_mutex_t *)task->wait_object;

        if (upstream_mutex->magic != FR_MUTEX_MAGIC) {
            break;
        }

        fr_task_t *const upstream_owner = upstream_mutex->owner;

        if ((upstream_owner == NULL) ||
            (upstream_owner == task)) {
            break;
        }

        task = upstream_owner;
    }
}

void fr_mutex_waiter_removed_locked(const void *wait_object) {
    if (wait_object == NULL) {
        return;
    }

    const fr_mutex_t *const mutex =
        (const fr_mutex_t *)wait_object;

    if ((mutex->magic != FR_MUTEX_MAGIC) ||
        (mutex->owner == NULL)) {
        return;
    }

    fr_mutex_recompute_priority_chain_locked(mutex->owner);
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

    if (mutex->owner == current_task) {
        fr_critical_exit(critical_state);
        return false;
    }

    if (timeout_ticks == 0u) {
        fr_critical_exit(critical_state);
        return false;
    }

    fr_task_t *const owner = mutex->owner;

    const bool blocked =
        fr_scheduler_block_current_locked(FR_WAIT_REASON_MUTEX,
                                          mutex,
                                          timeout_ticks);

    if (blocked) {
        fr_mutex_inherit_priority_chain_locked(owner,
                                               current_task->priority);
    }

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

        fr_mutex_recompute_priority_chain_locked(current_task);

        fr_critical_exit(critical_state);
        return true;
    }

    /*
     * Transfer ownership before waking the waiter. The mutex therefore
     * never becomes momentarily ownerless.
     */
    mutex->owner = waiter;

    if (!fr_scheduler_unblock_task_locked(
            waiter,
            FR_WAIT_RESULT_SIGNALED)) {
        mutex->owner = current_task;

        fr_critical_exit(critical_state);
        return false;
    }

    /*
     * The old owner no longer owns this mutex. Recalculate its
     * effective priority from its base priority and any remaining
     * mutex waiters on other mutexes that it still owns.
     */
    fr_mutex_recompute_priority_chain_locked(current_task);

    fr_scheduler_request_if_needed_locked();

    fr_critical_exit(critical_state);
    return true;
}