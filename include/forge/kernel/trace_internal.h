#ifndef FORGE_KERNEL_TRACE_INTERNAL_H
#define FORGE_KERNEL_TRACE_INTERNAL_H

#include <stdint.h>

#include "forge/trace.h"

/*
 * Internal event writer. The caller must already be serialized by a ForgeRTOS
 * critical section or execute in a kernel-aware exception handler. High-urgency
 * ISRs that are not allowed to call the kernel must not call this function.
 */
void fr_trace_record_locked(fr_trace_event_t event,
                     uint32_t task_id,
                     uint32_t arg0,
                     uint32_t arg1);

#endif
