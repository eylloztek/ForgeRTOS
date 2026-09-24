#ifndef FORGE_KERNEL_SCHEDULER_INTERNAL_H
#define FORGE_KERNEL_SCHEDULER_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>

#include "forge/kernel/task_internal.h"
#include "forge/kernel/wait_internal.h"

void fr_scheduler_tick_isr(void);

uint32_t *fr_scheduler_current_saved_sp(void);
uint32_t *fr_scheduler_switch_from_isr(uint32_t *current_saved_sp);

/*
 * Call only while kernel scheduling state is protected.
 * A successful block requests PendSV.
 */
bool fr_scheduler_block_current_locked(fr_wait_reason_t reason,
                                       const void *wait_object,
                                       uint32_t timeout_ticks);

/*
 * Complete a blocked task's wait.
 * Caller must protect kernel state and request rescheduling if needed.
 */
bool fr_scheduler_unblock_task_locked(fr_task_t *task,
                                     fr_wait_result_t result);

fr_task_t *fr_scheduler_select_waiter_locked(fr_wait_reason_t reason,
                                             const void *wait_object);

void fr_scheduler_request_if_needed_locked(void);

#endif