#ifndef FORGE_KERNEL_WAIT_TIMEOUT_INTERNAL_H
#define FORGE_KERNEL_WAIT_TIMEOUT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "forge/tick.h"

typedef struct {
    bool wait_forever;
    fr_tick_t deadline;
} fr_wait_timeout_t;

bool fr_wait_timeout_start(fr_wait_timeout_t *timeout,
                           uint32_t timeout_ticks);

bool fr_wait_timeout_remaining(const fr_wait_timeout_t *timeout,
                               uint32_t *remaining_ticks);

#endif