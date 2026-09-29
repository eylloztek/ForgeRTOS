#ifndef FORGE_EVENT_FLAGS_H
#define FORGE_EVENT_FLAGS_H

#include <stdbool.h>
#include <stdint.h>

#define FR_EVENT_FLAGS_WAIT_FOREVER UINT32_MAX

typedef struct {
    uint32_t magic;
    uint32_t bits;
} fr_event_flags_t;

/*
 * Initialize an event-flags object before starting the scheduler.
 */
bool fr_event_flags_init(fr_event_flags_t *flags);

/*
 * Set one or more bits.
 *
 * All blocked tasks whose wait conditions are satisfied are made ready.
 */
bool fr_event_flags_set(fr_event_flags_t *flags, uint32_t bits);

/*
 * Clear one or more bits.
 */
bool fr_event_flags_clear(fr_event_flags_t *flags, uint32_t bits);

/*
 * Read the current bit value.
 */
bool fr_event_flags_get(fr_event_flags_t *flags, uint32_t *bits);

/*
 * Wait for event bits.
 *
 * wait_all == false:
 *     Any requested bit is sufficient.
 *
 * wait_all == true:
 *     Every requested bit must be set.
 *
 * clear_on_exit:
 *     Clear the requested bits when the wait succeeds.
 *
 * matched_bits may be NULL.
 */
bool fr_event_flags_wait(fr_event_flags_t *flags,
                         uint32_t mask,
                         bool wait_all,
                         bool clear_on_exit,
                         uint32_t timeout_ticks,
                         uint32_t *matched_bits);

#endif