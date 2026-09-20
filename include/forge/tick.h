#ifndef FORGE_TICK_H
#define FORGE_TICK_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t fr_tick_t;

fr_tick_t fr_tick_now(void);
fr_tick_t fr_tick_elapsed(fr_tick_t start, fr_tick_t end);
bool fr_tick_has_elapsed(fr_tick_t start, fr_tick_t duration);

/*
 * Return true when a deadline has been reached.
 * The deadline must be within half of the 32-bit tick range.
 */
bool fr_tick_deadline_reached(fr_tick_t now, fr_tick_t deadline);

#endif