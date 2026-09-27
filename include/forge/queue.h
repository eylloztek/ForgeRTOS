#ifndef FORGE_QUEUE_H
#define FORGE_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint8_t *storage;
    uint32_t capacity;
    uint32_t item_size;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} fr_queue_t;

/*
 * Initialize an empty fixed-capacity queue.
 *
 * The storage belongs to the caller and must remain valid for the
 * entire lifetime of the queue.
 */
bool fr_queue_init(fr_queue_t *queue,
                   void *storage,
                   uint32_t capacity,
                   uint32_t item_size);

/*
 * Enqueue one item.
 *
 * Returns false when the queue is full or the operation is invalid.
 * This operation does not block.
 */
bool fr_queue_send(fr_queue_t *queue, const void *item);

/*
 * Dequeue the oldest item.
 *
 * Returns false when the queue is empty or the operation is invalid.
 * This operation does not block.
 */
bool fr_queue_receive(fr_queue_t *queue, void *item);

#endif