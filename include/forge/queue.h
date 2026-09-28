#ifndef FORGE_QUEUE_H
#define FORGE_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#define FR_QUEUE_WAIT_FOREVER UINT32_MAX

typedef struct {
    uint32_t magic;
    uint8_t *storage;
    uint32_t capacity;
    uint32_t item_size;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} fr_queue_t;

bool fr_queue_init(fr_queue_t *queue,
                   void *storage,
                   uint32_t capacity,
                   uint32_t item_size);

/*
 * Non-blocking operations.
 */
bool fr_queue_send(fr_queue_t *queue, const void *item);
bool fr_queue_receive(fr_queue_t *queue, void *item);

/*
 * Blocking operations.
 *
 * timeout_ticks == 0:
 *     Non-blocking behavior.
 *
 * timeout_ticks == FR_QUEUE_WAIT_FOREVER:
 *     Wait indefinitely.
 *
 * Finite timeout:
 *     Wait until the operation can complete or the deadline expires.
 */
bool fr_queue_send_wait(fr_queue_t *queue,
                        const void *item,
                        uint32_t timeout_ticks);

bool fr_queue_receive_wait(fr_queue_t *queue,
                           void *item,
                           uint32_t timeout_ticks);

#endif