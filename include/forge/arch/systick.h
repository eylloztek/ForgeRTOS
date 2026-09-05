#ifndef FORGE_ARCH_SYSTICK_H
#define FORGE_ARCH_SYSTICK_H

#include <stdbool.h>
#include <stdint.h>

bool fr_systick_init(uint32_t core_clock_hz, uint32_t tick_hz);
uint32_t fr_systick_get_ticks(void);

#endif