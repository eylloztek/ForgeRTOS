#ifndef FORGE_MUTEX_H
#define FORGE_MUTEX_H

#include <stdbool.h>
#include <stdint.h>

#include "forge/task.h"

#define FR_MUTEX_WAIT_FOREVER UINT32_MAX

typedef struct {
    uint32_t magic;
    fr_task_handle_t owner;
} fr_mutex_t;

/*
 * Initialize a mutex before starting the scheduler.
 */
bool fr_mutex_init(fr_mutex_t *mutex);

/*
 * Acquire the mutex.
 *
 * timeout_ticks == 0:
 *     Do not block.
 *
 * timeout_ticks == FR_MUTEX_WAIT_FOREVER:
 *     Wait indefinitely.
 *
 * Finite timeout:
 *     Block until ownership is obtained or the timeout expires.
 *
 * This mutex is intentionally non-recursive.
 */
bool fr_mutex_lock(fr_mutex_t *mutex, uint32_t timeout_ticks);

/*
 * Release the mutex.
 *
 * Only the current owner may unlock it.
 */
bool fr_mutex_unlock(fr_mutex_t *mutex);

#endif