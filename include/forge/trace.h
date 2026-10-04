#ifndef FORGE_TRACE_H
#define FORGE_TRACE_H

#include <stdbool.h>
#include <stdint.h>

#include "forge/tick.h"

#define FR_TRACE_BUFFER_CAPACITY 128u
#define FR_TRACE_TASK_ID_IDLE    0u

typedef enum {
    FR_TRACE_EVENT_NONE = 0u,
    FR_TRACE_EVENT_TASK_CREATE,
    FR_TRACE_EVENT_SCHEDULER_START,
    FR_TRACE_EVENT_TASK_BLOCK,
    FR_TRACE_EVENT_TASK_TIMEOUT,
    FR_TRACE_EVENT_TASK_UNBLOCK,
    FR_TRACE_EVENT_CONTEXT_SWITCH
} fr_trace_event_t;

typedef struct {
    uint32_t sequence;
    fr_tick_t tick;
    fr_trace_event_t event;
    uint32_t task_id;
    uint32_t arg0;
    uint32_t arg1;
} fr_trace_entry_t;

typedef struct {
    uint32_t capacity;
    uint32_t count;
    uint32_t write_index;
    uint32_t next_sequence;
    uint32_t overwritten_events;
    bool enabled;
} fr_trace_status_t;

/*
 * Initialize the in-memory trace buffer. Call before task creation so task-create
 * events are captured. Tracing is enabled after initialization.
 */
void fr_trace_init(void);

/* Enable or disable future trace recording without destroying buffered events. */
void fr_trace_enable(bool enabled);

/* Remove all buffered events and restart the sequence counter at one. */
void fr_trace_clear(void);

/* Read trace-buffer metadata. */
bool fr_trace_get_status(fr_trace_status_t *out_status);

/*
 * Copy the newest events into entries in chronological order. If max_entries is
 * smaller than the number of buffered events, only the newest max_entries are
 * returned.
 */
uint32_t fr_trace_snapshot(fr_trace_entry_t *entries, uint32_t max_entries);

#endif
