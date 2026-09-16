#ifndef FORGE_KERNEL_SCHEDULER_INTERNAL_H
#define FORGE_KERNEL_SCHEDULER_INTERNAL_H

#include <stdint.h>

void fr_scheduler_tick_isr(void);

uint32_t *fr_scheduler_current_saved_sp(void);
uint32_t *fr_scheduler_switch_from_isr(uint32_t *current_saved_sp);

#endif