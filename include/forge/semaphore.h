#ifndef FORGE_SEMAPHORE_H
#define FORGE_SEMAPHORE_H

#include <stdbool.h>
#include <stdint.h>

#define FR_SEMAPHORE_WAIT_FOREVER UINT32_MAX

#define FR_BINARY_SEMAPHORE_WAIT_FOREVER \
    FR_SEMAPHORE_WAIT_FOREVER

#define FR_COUNTING_SEMAPHORE_WAIT_FOREVER \
    FR_SEMAPHORE_WAIT_FOREVER

typedef struct {
    uint32_t magic;
    uint32_t available;
} fr_binary_semaphore_t;

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t max_count;
} fr_counting_semaphore_t;

/* Binary semaphore API. */

bool fr_binary_semaphore_init(fr_binary_semaphore_t *semaphore,
                              bool initially_available);

bool fr_binary_semaphore_take(fr_binary_semaphore_t *semaphore,
                              uint32_t timeout_ticks);

bool fr_binary_semaphore_give(fr_binary_semaphore_t *semaphore);

/* Counting semaphore API. */

bool fr_counting_semaphore_init(fr_counting_semaphore_t *semaphore,
                                uint32_t initial_count,
                                uint32_t max_count);

bool fr_counting_semaphore_take(fr_counting_semaphore_t *semaphore,
                                uint32_t timeout_ticks);

bool fr_counting_semaphore_give(fr_counting_semaphore_t *semaphore);

#endif