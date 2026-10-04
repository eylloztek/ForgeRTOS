#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/critical.h"
#include "forge/kernel/trace_internal.h"
#include "forge/tick.h"
#include "forge/trace.h"

_Static_assert(FR_TRACE_BUFFER_CAPACITY > 0u,
               "Trace buffer must contain at least one entry");

static fr_trace_entry_t g_fr_trace_buffer[FR_TRACE_BUFFER_CAPACITY];
static volatile uint32_t g_fr_trace_write_index;
static volatile uint32_t g_fr_trace_count;
static volatile uint32_t g_fr_trace_next_sequence;
static volatile uint32_t g_fr_trace_overwritten_events;
static volatile bool g_fr_trace_enabled;

static uint32_t fr_trace_advance_index(uint32_t index) {
    ++index;

    if (index == FR_TRACE_BUFFER_CAPACITY) {
        index = 0u;
    }

    return index;
}

static void fr_trace_copy_entry(fr_trace_entry_t *destination,
                                const fr_trace_entry_t *source) {
    destination->sequence = source->sequence;
    destination->tick = source->tick;
    destination->event = source->event;
    destination->task_id = source->task_id;
    destination->arg0 = source->arg0;
    destination->arg1 = source->arg1;
}

void fr_trace_init(void) {
    g_fr_trace_write_index = 0u;
    g_fr_trace_count = 0u;
    g_fr_trace_next_sequence = 1u;
    g_fr_trace_overwritten_events = 0u;
    g_fr_trace_enabled = true;
}

void fr_trace_enable(bool enabled) {
    const fr_critical_state_t critical_state = fr_critical_enter();
    g_fr_trace_enabled = enabled;
    fr_critical_exit(critical_state);
}

void fr_trace_clear(void) {
    const fr_critical_state_t critical_state = fr_critical_enter();

    g_fr_trace_write_index = 0u;
    g_fr_trace_count = 0u;
    g_fr_trace_next_sequence = 1u;
    g_fr_trace_overwritten_events = 0u;

    fr_critical_exit(critical_state);
}

bool fr_trace_get_status(fr_trace_status_t *out_status) {
    if (out_status == NULL) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();

    out_status->capacity = FR_TRACE_BUFFER_CAPACITY;
    out_status->count = g_fr_trace_count;
    out_status->write_index = g_fr_trace_write_index;
    out_status->next_sequence = g_fr_trace_next_sequence;
    out_status->overwritten_events = g_fr_trace_overwritten_events;
    out_status->enabled = g_fr_trace_enabled;

    fr_critical_exit(critical_state);
    return true;
}

uint32_t fr_trace_snapshot(fr_trace_entry_t *entries,
                           uint32_t max_entries) {
    if ((entries == NULL) || (max_entries == 0u)) {
        return 0u;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();

    const uint32_t count = g_fr_trace_count;
    const uint32_t copy_count =
        (count < max_entries) ? count : max_entries;

    uint32_t oldest_index = g_fr_trace_write_index;

    if (count < FR_TRACE_BUFFER_CAPACITY) {
        oldest_index = 0u;
    }

    const uint32_t skip_count = count - copy_count;

    for (uint32_t i = 0u; i < skip_count; ++i) {
        oldest_index = fr_trace_advance_index(oldest_index);
    }

    for (uint32_t i = 0u; i < copy_count; ++i) {
        fr_trace_copy_entry(&entries[i],
                            &g_fr_trace_buffer[oldest_index]);
        oldest_index = fr_trace_advance_index(oldest_index);
    }

    fr_critical_exit(critical_state);
    return copy_count;
}

void fr_trace_record_locked(fr_trace_event_t event,
                            uint32_t task_id,
                            uint32_t arg0,
                            uint32_t arg1) {
    if (!g_fr_trace_enabled ||
        (event <= FR_TRACE_EVENT_NONE) ||
        (event > FR_TRACE_EVENT_CONTEXT_SWITCH)) {
        return;
    }

    const uint32_t index = g_fr_trace_write_index;
    fr_trace_entry_t *const entry = &g_fr_trace_buffer[index];

    entry->sequence = g_fr_trace_next_sequence;
    entry->tick = fr_tick_now();
    entry->event = event;
    entry->task_id = task_id;
    entry->arg0 = arg0;
    entry->arg1 = arg1;

    ++g_fr_trace_next_sequence;
    g_fr_trace_write_index = fr_trace_advance_index(index);

    if (g_fr_trace_count < FR_TRACE_BUFFER_CAPACITY) {
        ++g_fr_trace_count;
    } else {
        ++g_fr_trace_overwritten_events;
    }
}
