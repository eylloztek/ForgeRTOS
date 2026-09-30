#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/kernel/wait_internal.h"
#include "forge/kernel/wait_timeout_internal.h"
#include "forge/tick.h"

bool fr_wait_timeout_start(fr_wait_timeout_t *timeout,
                           uint32_t timeout_ticks) {
    if (timeout == NULL) {
        return false;
    }

    if ((timeout_ticks > FR_WAIT_MAX_FINITE_TICKS) &&
        (timeout_ticks != FR_WAIT_FOREVER)) {
        return false;
    }

    timeout->wait_forever =
        (timeout_ticks == FR_WAIT_FOREVER);

    if (timeout->wait_forever) {
        timeout->deadline = 0u;
    } else {
        timeout->deadline =
            fr_tick_now() + timeout_ticks;
    }

    return true;
}

bool fr_wait_timeout_remaining(const fr_wait_timeout_t *timeout,
                               uint32_t *remaining_ticks) {
    if ((timeout == NULL) ||
        (remaining_ticks == NULL)) {
        return false;
    }

    if (timeout->wait_forever) {
        *remaining_ticks = FR_WAIT_FOREVER;
        return true;
    }

    const fr_tick_t now = fr_tick_now();

    if (fr_tick_deadline_reached(
            now,
            timeout->deadline)) {
        *remaining_ticks = 0u;
        return false;
    }

    *remaining_ticks =
        (uint32_t)(timeout->deadline - now);

    return true;
}
