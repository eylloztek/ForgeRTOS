#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "forge/critical.h"
#include "forge/queue.h"
#include "forge/scheduler.h"

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

bool fr_queue_send(fr_queue_t *queue, const void *item) {
    if ((queue == NULL) ||
        (item == NULL) ||
        !fr_queue_thread_context_allowed()) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();
    const fr_task_handle_t current_task = fr_scheduler_current_task();

    if ((critical_state != 0u) ||
        !fr_scheduler_is_running() ||
        (current_task == NULL) ||
        !fr_queue_state_valid_locked(queue)) {
        fr_critical_exit(critical_state);
        return false;
    }

    if (queue->count == queue->capacity) {
        fr_critical_exit(critical_state);
        return false;
    }

    const uint32_t offset = queue->tail * queue->item_size;

    fr_queue_copy_bytes(&queue->storage[offset],
                        item,
                        queue->item_size);

    queue->tail =
        fr_queue_next_index(queue->tail, queue->capacity);

    ++queue->count;

    fr_critical_exit(critical_state);
    return true;
}

bool fr_queue_receive(fr_queue_t *queue, void *item) {
    if ((queue == NULL) ||
        (item == NULL) ||
        !fr_queue_thread_context_allowed()) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();
    const fr_task_handle_t current_task = fr_scheduler_current_task();

    if ((critical_state != 0u) ||
        !fr_scheduler_is_running() ||
        (current_task == NULL) ||
        !fr_queue_state_valid_locked(queue)) {
        fr_critical_exit(critical_state);
        return false;
    }

    if (queue->count == 0u) {
        fr_critical_exit(critical_state);
        return false;
    }

    const uint32_t offset = queue->head * queue->item_size;

    fr_queue_copy_bytes(item,
                        &queue->storage[offset],
                        queue->item_size);

    queue->head =
        fr_queue_next_index(queue->head, queue->capacity);

    --queue->count;

    fr_critical_exit(critical_state);
    return true;
}