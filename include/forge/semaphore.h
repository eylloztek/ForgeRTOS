#ifndef FORGE_SEMAPHORE_H
#define FORGE_SEMAPHORE_H

#include <stdbool.h>
#include <stdint.h>

#define FR_BINARY_SEMAPHORE_WAIT_FOREVER UINT32_MAX

typedef struct {
    uint32_t magic;
    uint32_t available;
} fr_binary_semaphore_t;

/* Initialize before starting the scheduler. */
bool fr_binary_semaphore_init(fr_binary_semaphore_t *semaphore,
                              bool initially_available);

/* Take immediately, wait for a signal, or return false on timeout. */
bool fr_binary_semaphore_take(fr_binary_semaphore_t *semaphore,
                              uint32_t timeout_ticks);

/* Release one token or directly transfer it to a waiting task. */
bool fr_binary_semaphore_give(fr_binary_semaphore_t *semaphore);

#endif