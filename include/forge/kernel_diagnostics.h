#ifndef FORGE_KERNEL_DIAGNOSTICS_H
#define FORGE_KERNEL_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#define FR_KERNEL_INVARIANT_TASK_COUNT        (1u << 0)
#define FR_KERNEL_INVARIANT_TASK_POINTER      (1u << 1)
#define FR_KERNEL_INVARIANT_TASK_ID           (1u << 2)
#define FR_KERNEL_INVARIANT_TASK_PRIORITY     (1u << 3)
#define FR_KERNEL_INVARIANT_TASK_STATE        (1u << 4)
#define FR_KERNEL_INVARIANT_STACK_GEOMETRY    (1u << 5)
#define FR_KERNEL_INVARIANT_SAVED_SP          (1u << 6)
#define FR_KERNEL_INVARIANT_WAIT_STATE        (1u << 7)
#define FR_KERNEL_INVARIANT_RUNNING_COUNT     (1u << 8)
#define FR_KERNEL_INVARIANT_CURRENT_TASK      (1u << 9)

#define FR_KERNEL_INVARIANT_NO_TASK_ID UINT32_MAX

typedef struct {
    uint32_t task_count;
    uint32_t created_tasks;
    uint32_t ready_tasks;
    uint32_t running_tasks;
    uint32_t blocked_tasks;
    uint32_t suspended_tasks;
    uint32_t current_task_id;
    uint32_t violation_mask;
    uint32_t first_bad_task_id;
    bool scheduler_running;
} fr_kernel_invariant_report_t;

/*
 * Scan the registered task set and verify kernel state relationships.
 * This is an explicit diagnostic operation; it is not executed from SysTick.
 */
bool fr_kernel_check_invariants(fr_kernel_invariant_report_t *out_report);

#endif
