#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "forge/critical.h"
#include "forge/kernel/scheduler_internal.h"
#include "forge/kernel/task_internal.h"
#include "forge/kernel/wait_internal.h"
#include "forge/kernel/wait_timeout_internal.h"
#include "forge/queue.h"
#include "forge/scheduler.h"
#include "forge/tick.h"

#define FR_QUEUE_MAGIC 0x46525155u

static bool fr_queue_thread_context_allowed(void) {
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

static bool fr_queue_state_valid_locked(const fr_queue_t *queue) {
    if ((queue->magic != FR_QUEUE_MAGIC) ||
        (queue->storage == NULL) ||
        (queue->capacity == 0u) ||
        (queue->item_size == 0u)) {
        return false;
    }

    if (queue->capacity > (UINT32_MAX / queue->item_size)) {
        return false;
    }

    if ((queue->head >= queue->capacity) ||
        (queue->tail >= queue->capacity) ||
        (queue->count > queue->capacity)) {
        return false;
    }

    return true;
}

static uint32_t fr_queue_next_index(uint32_t index,
                                    uint32_t capacity) {
    ++index;

    if (index == capacity) {
        index = 0u;
    }

    return index;
}

static void fr_queue_copy_bytes(void *destination,
                                const void *source,
                                uint32_t size) {
    uint8_t *const destination_bytes = destination;
    const uint8_t *const source_bytes = source;

    for (uint32_t i = 0u; i < size; ++i) {
        destination_bytes[i] = source_bytes[i];
    }
}

static void fr_queue_wake_waiter_locked(fr_wait_reason_t reason,
                                        const fr_queue_t *queue) {
    fr_task_t *const waiter =
        fr_scheduler_select_waiter_locked(reason, queue);

    if (waiter == NULL) {
        return;
    }

    if (fr_scheduler_unblock_task_locked(
            waiter,
            FR_WAIT_RESULT_SIGNALED)) {
        fr_scheduler_request_if_needed_locked();
    }
}

static bool fr_queue_try_send_locked(fr_queue_t *queue,
                                     const void *item) {
    if (queue->count == queue->capacity) {
        return false;
    }

    const uint32_t offset = queue->tail * queue->item_size;

    fr_queue_copy_bytes(&queue->storage[offset],
                        item,
                        queue->item_size);

    queue->tail =
        fr_queue_next_index(queue->tail,
                            queue->capacity);

    ++queue->count;

    /*
     * The queue now contains at least one item.
     * Wake one task waiting for data.
     */
    fr_queue_wake_waiter_locked(
        FR_WAIT_REASON_QUEUE_RECEIVE,
        queue);

    return true;
}

static bool fr_queue_try_receive_locked(fr_queue_t *queue,
                                        void *item) {
    if (queue->count == 0u) {
        return false;
    }

    const uint32_t offset = queue->head * queue->item_size;

    fr_queue_copy_bytes(item,
                        &queue->storage[offset],
                        queue->item_size);

    queue->head =
        fr_queue_next_index(queue->head,
                            queue->capacity);

    --queue->count;

    /*
     * The queue now contains at least one free slot.
     * Wake one task waiting to send.
     */
    fr_queue_wake_waiter_locked(
        FR_WAIT_REASON_QUEUE_SEND,
        queue);

    return true;
}

bool fr_queue_init(fr_queue_t *queue,
                   void *storage,
                   uint32_t capacity,
                   uint32_t item_size) {
    if ((queue == NULL) ||
        (storage == NULL) ||
        (capacity == 0u) ||
        (item_size == 0u) ||
        (capacity > (UINT32_MAX / item_size)) ||
        fr_scheduler_is_running()) {
        return false;
    }

    queue->magic = 0u;
    queue->storage = storage;
    queue->capacity = capacity;
    queue->item_size = item_size;
    queue->head = 0u;
    queue->tail = 0u;
    queue->count = 0u;
    queue->magic = FR_QUEUE_MAGIC;

    return true;
}

bool fr_queue_send_wait(fr_queue_t *queue,
                        const void *item,
                        uint32_t timeout_ticks) {
    if ((queue == NULL) ||
        (item == NULL) ||
        !fr_queue_thread_context_allowed()) {
        return false;
    }

    fr_wait_timeout_t timeout;

    if (!fr_wait_timeout_start(
            &timeout,
            timeout_ticks)) {
        return false;
    }

    for (;;) {
        const fr_critical_state_t critical_state =
            fr_critical_enter();

        fr_task_t *const current_task =
            fr_scheduler_current_task();

        if ((critical_state != 0u) ||
            !fr_scheduler_is_running() ||
            (current_task == NULL) ||
            !fr_queue_state_valid_locked(queue)) {
            fr_critical_exit(critical_state);
            return false;
        }

        if (fr_queue_try_send_locked(queue, item)) {
            fr_critical_exit(critical_state);
            return true;
        }

        uint32_t wait_ticks;

        if (!fr_wait_timeout_remaining(
                &timeout,
                &wait_ticks)) {
            fr_critical_exit(critical_state);
            return false;
        }

        const bool blocked =
            fr_scheduler_block_current_locked(
                FR_WAIT_REASON_QUEUE_SEND,
                queue,
                wait_ticks);

        fr_critical_exit(critical_state);

        if (!blocked) {
            return false;
        }

        if (current_task->wait_result ==
            FR_WAIT_RESULT_TIMEOUT) {
            return false;
        }

        if (current_task->wait_result !=
            FR_WAIT_RESULT_SIGNALED) {
            return false;
        }

        /*
         * A signal does not reserve a queue slot.
         * Retry using the original absolute deadline.
         */
    }
}

bool fr_queue_receive_wait(fr_queue_t *queue,
                           void *item,
                           uint32_t timeout_ticks) {
    if ((queue == NULL) ||
        (item == NULL) ||
        !fr_queue_thread_context_allowed()) {
        return false;
    }

    fr_wait_timeout_t timeout;

    if (!fr_wait_timeout_start(
            &timeout,
            timeout_ticks)) {
        return false;
    }

    for (;;) {
        const fr_critical_state_t critical_state =
            fr_critical_enter();

        fr_task_t *const current_task =
            fr_scheduler_current_task();

        if ((critical_state != 0u) ||
            !fr_scheduler_is_running() ||
            (current_task == NULL) ||
            !fr_queue_state_valid_locked(queue)) {
            fr_critical_exit(critical_state);
            return false;
        }

        if (fr_queue_try_receive_locked(queue, item)) {
            fr_critical_exit(critical_state);
            return true;
        }

        uint32_t wait_ticks;

        if (!fr_wait_timeout_remaining(
                &timeout,
                &wait_ticks)) {
            fr_critical_exit(critical_state);
            return false;
        }

        const bool blocked =
            fr_scheduler_block_current_locked(
                FR_WAIT_REASON_QUEUE_RECEIVE,
                queue,
                wait_ticks);

        fr_critical_exit(critical_state);

        if (!blocked) {
            return false;
        }

        if (current_task->wait_result ==
            FR_WAIT_RESULT_TIMEOUT) {
            return false;
        }

        if (current_task->wait_result !=
            FR_WAIT_RESULT_SIGNALED) {
            return false;
        }

        /*
         * A signal does not reserve a queued item.
         * Retry using the original absolute deadline.
         */
    }
}

bool fr_queue_send(fr_queue_t *queue,
                   const void *item) {
    return fr_queue_send_wait(queue,
                              item,
                              0u);
}

bool fr_queue_receive(fr_queue_t *queue,
                      void *item) {
    return fr_queue_receive_wait(queue,
                                 item,
                                 0u);
}