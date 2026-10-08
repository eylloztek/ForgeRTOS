#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/arch/cortex_m4.h"
#include "forge/arch/fault.h"
#include "forge/arch/systick.h"
#include "forge/assert.h"
#include "forge/board.h"
#include "forge/event_flags.h"
#include "forge/kernel_diagnostics.h"
#include "forge/scheduler.h"
#include "forge/semaphore.h"
#include "forge/mutex.h"
#include "forge/queue.h"
#include "forge/task.h"
#include "forge/task_stack.h"
#include "forge/tick.h"
#include "forge/trace.h"
#include "forge/uart.h"
#include "stress.h"

#define FR_DEMO_BOOTSTRAP_STACK_WORDS     128u
#define FR_DEMO_STACK_PATTERN             0xA5A5A5A5u
#define FR_DEMO_TICK_HZ                   1000u
#define FR_DEMO_LED_TOGGLE_TICKS          500u

#define FR_DEMO_TASK_A_STACK_WORDS        128u
#define FR_DEMO_TASK_B_STACK_WORDS        96u
#define FR_DEMO_TASK_C_STACK_WORDS        96u

#define FR_DEMO_VALIDATION_MASK           0x0000000Fu
#define FR_DEMO_SCHEDULER_NOT_RETURNED    0xFFu

#define FR_DEMO_TURN_TASK_A                1u
#define FR_DEMO_TURN_TASK_B                2u

#define FR_DEMO_TASK_A_LOCAL_STATE_SEED    0x13579BDFu
#define FR_DEMO_TASK_B_LOCAL_STATE_SEED    0x2468ACE0u

#ifndef FR_DEMO_TASK_A_PRIORITY
#define FR_DEMO_TASK_A_PRIORITY 3u
#endif

#ifndef FR_DEMO_TASK_B_PRIORITY
#define FR_DEMO_TASK_B_PRIORITY 9u
#endif

#ifndef FR_DEMO_TASK_C_PRIORITY
#define FR_DEMO_TASK_C_PRIORITY 6u
#endif

#define FR_DEMO_INVERSION_MEDIUM_RUN_TICKS 20u

#ifndef FR_DEMO_IDLE_ONLY
#define FR_DEMO_IDLE_ONLY 0u
#endif

#define FR_DEMO_TASK_A_SLEEP_TICKS 5u
#define FR_DEMO_TASK_B_SLEEP_TICKS 13u

#define FR_DEMO_MUTEX_HOLD_TICKS       8u
#define FR_DEMO_MUTEX_TIMEOUT_TICKS    2u

#define FR_DEMO_QUEUE_CAPACITY       4u
#define FR_DEMO_QUEUE_PROBE_CAPACITY 3u

#define FR_DEMO_BLOCKING_QUEUE_CAPACITY        1u
#define FR_DEMO_BLOCKING_QUEUE_TIMEOUT_TICKS   2u

#define FR_DEMO_BLOCKING_QUEUE_VALUE_A   0xA5A50001u
#define FR_DEMO_BLOCKING_QUEUE_VALUE_B1  0xB5B50001u
#define FR_DEMO_BLOCKING_QUEUE_VALUE_B2  0xB5B50002u

#define FR_DEMO_EVENT_FLAG_A (1u << 0)
#define FR_DEMO_EVENT_FLAG_B (1u << 1)

#define FR_DEMO_EVENT_FLAGS_AB \
    (FR_DEMO_EVENT_FLAG_A | FR_DEMO_EVENT_FLAG_B)

#define FR_DEMO_EVENT_FLAGS_TIMEOUT_TICKS 2u

#define FR_DEMO_IPC_TIMEOUT_TICKS 3u
#define FR_DEMO_STACK_DIAGNOSTIC_MASK 0x0000000Fu
#define FR_DEMO_KERNEL_INVARIANT_MASK  0x0000000Fu
#define FR_DEMO_TRACE_DIAGNOSTIC_MASK   0x0000000Fu
#define FR_DEMO_TRACE_SNAPSHOT_CAPACITY 16u

#define FR_DEMO_UART_BAUD_RATE             115200u
#define FR_DEMO_MONITOR_TASK_STACK_WORDS   128u
#define FR_DEMO_MONITOR_TASK_PRIORITY      1u
#define FR_DEMO_MONITOR_POLL_TICKS         50u
#define FR_DEMO_MONITOR_TRACE_CAPACITY     8u
#define FR_DEMO_UART_LINE_CAPACITY         192u

_Alignas(8) uint32_t g_fr_demo_bootstrap_stack[FR_DEMO_BOOTSTRAP_STACK_WORDS];
_Alignas(8) uint32_t g_fr_demo_task_a_stack[FR_DEMO_TASK_A_STACK_WORDS];
_Alignas(8) uint32_t g_fr_demo_task_b_stack[FR_DEMO_TASK_B_STACK_WORDS];
_Alignas(8) uint32_t g_fr_demo_task_c_stack[FR_DEMO_TASK_C_STACK_WORDS];
_Alignas(8) uint32_t g_fr_demo_monitor_task_stack[FR_DEMO_MONITOR_TASK_STACK_WORDS];

uint32_t g_fr_demo_task_a_argument = 0xA1A1A1A1u;
uint32_t g_fr_demo_task_b_argument = 0xB2B2B2B2u;

static const uint32_t g_fr_demo_task_a_stack_pattern[4] = {
    0xA0A0A0A0u,
    0xA1A1A1A1u,
    0xA2A2A2A2u,
    0xA3A3A3A3u
};

static const uint32_t g_fr_demo_task_b_stack_pattern[4] = {
    0xB0B0B0B0u,
    0xB1B1B1B1u,
    0xB2B2B2B2u,
    0xB3B3B3B3u
};

typedef struct {
    uint32_t validations;
    uint32_t execution_errors;
    uint32_t stack_errors;
    uint32_t local_state_errors;
    uint32_t local_state_snapshot;
} fr_demo_preemption_stats_t;

typedef struct {
    uint32_t sequence;
    fr_tick_t produced_tick;
} fr_demo_queue_message_t;

fr_task_handle_t g_fr_demo_task_a;
fr_task_handle_t g_fr_demo_task_b;

fr_task_info_t g_fr_demo_task_a_info;
fr_task_info_t g_fr_demo_task_b_info;

fr_task_status_t g_fr_demo_task_a_status;
fr_task_status_t g_fr_demo_task_b_status;

fr_task_handle_t g_fr_demo_task_c;
fr_task_status_t g_fr_demo_task_c_status;

fr_task_handle_t g_fr_demo_monitor_task;
fr_task_info_t g_fr_demo_monitor_task_info;
fr_task_status_t g_fr_demo_monitor_task_status;

volatile uint32_t g_fr_demo_task_a_started;
volatile uint32_t g_fr_demo_task_b_started;

volatile uint32_t g_fr_demo_task_a_iterations;
volatile uint32_t g_fr_demo_task_b_iterations;

volatile uint32_t g_fr_demo_task_a_argument_value;
volatile uint32_t g_fr_demo_task_b_argument_value;

volatile fr_demo_preemption_stats_t g_fr_demo_task_a_preemption;
volatile fr_demo_preemption_stats_t g_fr_demo_task_b_preemption;

volatile uint32_t g_fr_demo_task_a_entry_count;
volatile uint32_t g_fr_demo_task_b_entry_count;
volatile uint32_t g_fr_demo_task_c_entry_count;
volatile uint32_t g_fr_demo_monitor_task_entry_count;

volatile uint32_t g_fr_demo_task_a_sleep_calls;
volatile uint32_t g_fr_demo_task_a_wakeups;
volatile uint32_t g_fr_demo_task_a_sleep_errors;

volatile uint32_t g_fr_demo_task_b_sleep_calls;
volatile uint32_t g_fr_demo_task_b_wakeups;
volatile uint32_t g_fr_demo_task_b_sleep_errors;

volatile uint32_t g_fr_demo_timeout_math_failures;

volatile uint32_t g_fr_demo_task_a_last_sleep_elapsed;
volatile uint32_t g_fr_demo_task_b_last_sleep_elapsed;

uint32_t g_fr_demo_task_count_snapshot;

volatile fr_scheduler_status_t g_fr_demo_scheduler_return_status = FR_DEMO_SCHEDULER_NOT_RETURNED;

static fr_binary_semaphore_t g_fr_demo_binary_semaphore;
static fr_binary_semaphore_t g_fr_demo_semaphore_probe;

volatile uint32_t g_fr_demo_semaphore_init_failures;
volatile uint32_t g_fr_demo_semaphore_probe_failures;

volatile uint32_t g_fr_demo_semaphore_give_successes;
volatile uint32_t g_fr_demo_semaphore_give_full;

volatile uint32_t g_fr_demo_semaphore_initial_empty_checks;
volatile uint32_t g_fr_demo_semaphore_initial_timeouts;
volatile uint32_t g_fr_demo_semaphore_forever_signals;

volatile uint32_t g_fr_demo_semaphore_take_successes;
volatile uint32_t g_fr_demo_semaphore_take_timeouts;

static fr_counting_semaphore_t g_fr_demo_counting_semaphore;
static fr_counting_semaphore_t g_fr_demo_counting_probe;
static fr_counting_semaphore_t g_fr_demo_counting_invalid_probe;

static fr_mutex_t g_fr_demo_inversion_mutex;

static fr_binary_semaphore_t g_fr_demo_inversion_high_gate;
static fr_binary_semaphore_t g_fr_demo_inversion_medium_gate;
static fr_binary_semaphore_t g_fr_demo_mutex_start_gate;

volatile uint32_t g_fr_demo_counting_init_failures;
volatile uint32_t g_fr_demo_counting_validation_failures;
volatile uint32_t g_fr_demo_counting_probe_failures;

volatile uint32_t g_fr_demo_counting_give_successes;
volatile uint32_t g_fr_demo_counting_wait_successes;
volatile uint32_t g_fr_demo_counting_wait_errors;

static fr_mutex_t g_fr_demo_mutex;
static fr_mutex_t g_fr_demo_mutex_timeout_probe;

volatile uint32_t g_fr_demo_mutex_init_failures;

volatile uint32_t g_fr_demo_mutex_a_lock_successes;
volatile uint32_t g_fr_demo_mutex_a_unlock_successes;
volatile uint32_t g_fr_demo_mutex_a_recursive_rejections;

volatile uint32_t g_fr_demo_mutex_b_timeout_observed;
volatile uint32_t g_fr_demo_mutex_b_non_owner_rejections;
volatile uint32_t g_fr_demo_mutex_b_lock_successes;
volatile uint32_t g_fr_demo_mutex_b_unlock_successes;

volatile uint32_t g_fr_demo_mutex_handoff_errors;
volatile uint32_t g_fr_demo_mutex_errors;

volatile uint32_t g_fr_demo_inversion_init_failures;
volatile uint32_t g_fr_demo_inversion_errors;

volatile uint32_t g_fr_demo_inversion_low_lock_successes;
volatile uint32_t g_fr_demo_inversion_low_unlock_successes;

volatile uint32_t g_fr_demo_inversion_high_wait_started;
volatile uint32_t g_fr_demo_inversion_high_lock_successes;
volatile uint32_t g_fr_demo_inversion_high_unlock_successes;

volatile uint32_t g_fr_demo_inversion_medium_started;
volatile uint32_t g_fr_demo_inversion_medium_completed;
volatile uint32_t g_fr_demo_inversion_medium_iterations;

volatile uint32_t g_fr_demo_inversion_low_lock_tick;
volatile uint32_t g_fr_demo_inversion_low_unlock_tick;

volatile uint32_t g_fr_demo_inversion_high_wait_start_tick;
volatile uint32_t g_fr_demo_inversion_high_acquire_tick;
volatile uint32_t g_fr_demo_inversion_high_wait_elapsed;

volatile uint32_t g_fr_demo_inversion_medium_start_tick;
volatile uint32_t g_fr_demo_inversion_medium_end_tick;
volatile uint32_t g_fr_demo_inversion_medium_run_elapsed;

volatile uint32_t g_fr_demo_inheritance_owner_boosted;
volatile uint32_t g_fr_demo_inheritance_owner_restored;
volatile uint32_t g_fr_demo_inheritance_high_before_medium;
volatile uint32_t g_fr_demo_inheritance_observed;

volatile uint32_t g_fr_demo_inheritance_low_effective_priority;
volatile uint32_t g_fr_demo_inheritance_low_base_priority;
volatile uint32_t g_fr_demo_inheritance_low_priority_after_unlock;

static fr_queue_t g_fr_demo_queue;
static fr_demo_queue_message_t g_fr_demo_queue_storage[FR_DEMO_QUEUE_CAPACITY];
static fr_queue_t g_fr_demo_queue_probe;
static uint32_t g_fr_demo_queue_probe_storage[FR_DEMO_QUEUE_PROBE_CAPACITY];
static fr_queue_t g_fr_demo_queue_invalid_probe;
static uint32_t g_fr_demo_queue_invalid_storage[1];

volatile uint32_t g_fr_demo_queue_init_failures;
volatile uint32_t g_fr_demo_queue_validation_failures;
volatile uint32_t g_fr_demo_queue_probe_failures;

volatile uint32_t g_fr_demo_queue_send_successes;
volatile uint32_t g_fr_demo_queue_send_full;

volatile uint32_t g_fr_demo_queue_receive_successes;
volatile uint32_t g_fr_demo_queue_receive_empty;
volatile uint32_t g_fr_demo_queue_order_errors;

volatile uint32_t g_fr_demo_queue_next_sequence;
volatile uint32_t g_fr_demo_queue_last_sent_sequence;
volatile uint32_t g_fr_demo_queue_last_received_sequence;

volatile uint32_t g_fr_demo_queue_last_produced_tick;
volatile uint32_t g_fr_demo_queue_last_received_tick;
volatile uint32_t g_fr_demo_queue_last_latency;

static fr_queue_t g_fr_demo_blocking_queue;

static uint32_t
    g_fr_demo_blocking_queue_storage[
        FR_DEMO_BLOCKING_QUEUE_CAPACITY];

volatile uint32_t g_fr_demo_blocking_queue_init_failures;
volatile uint32_t g_fr_demo_blocking_queue_errors;
volatile uint32_t g_fr_demo_blocking_queue_data_errors;

volatile uint32_t g_fr_demo_blocking_queue_receive_timeout_observed;
volatile uint32_t g_fr_demo_blocking_queue_receive_wait_successes;
volatile uint32_t g_fr_demo_blocking_queue_send_timeout_observed;

volatile uint32_t g_fr_demo_blocking_queue_send_wait_successes;
volatile uint32_t g_fr_demo_blocking_queue_release_send_successes;
volatile uint32_t g_fr_demo_blocking_queue_release_receive_successes;
volatile uint32_t g_fr_demo_blocking_queue_receive_waiting;
volatile uint32_t g_fr_demo_blocking_queue_send_waiting;
volatile uint32_t g_fr_demo_blocking_queue_receive_released;
volatile uint32_t g_fr_demo_blocking_queue_send_released;

static fr_event_flags_t g_fr_demo_event_flags;

volatile uint32_t g_fr_demo_event_flags_init_failures;
volatile uint32_t g_fr_demo_event_flags_errors;
volatile uint32_t g_fr_demo_event_flags_data_errors;

volatile uint32_t g_fr_demo_event_flags_timeout_observed;
volatile uint32_t g_fr_demo_event_flags_wait_any_successes;
volatile uint32_t g_fr_demo_event_flags_wait_all_successes;

volatile uint32_t g_fr_demo_event_flags_set_successes;
volatile uint32_t g_fr_demo_event_flags_clear_successes;

volatile uint32_t g_fr_demo_event_flags_any_requested;
volatile uint32_t g_fr_demo_event_flags_any_released;

volatile uint32_t g_fr_demo_event_flags_all_requested;
volatile uint32_t g_fr_demo_event_flags_all_stage;

volatile uint32_t g_fr_demo_event_flags_any_match;
volatile uint32_t g_fr_demo_event_flags_all_match;
volatile uint32_t g_fr_demo_event_flags_final_bits;

static fr_binary_semaphore_t g_fr_demo_ipc_timeout_binary;
static fr_counting_semaphore_t g_fr_demo_ipc_timeout_counting;

volatile uint32_t g_fr_demo_ipc_timeout_init_failures;
volatile uint32_t g_fr_demo_ipc_timeout_errors;

volatile uint32_t g_fr_demo_ipc_timeout_binary_observed;
volatile uint32_t g_fr_demo_ipc_timeout_counting_observed;

volatile uint32_t g_fr_demo_ipc_timeout_binary_verified;
volatile uint32_t g_fr_demo_ipc_timeout_counting_verified;
volatile uint32_t g_fr_demo_ipc_timeout_mutex_verified;
volatile uint32_t g_fr_demo_ipc_timeout_queue_receive_verified;
volatile uint32_t g_fr_demo_ipc_timeout_queue_send_verified;
volatile uint32_t g_fr_demo_ipc_timeout_event_flags_verified;

volatile uint32_t g_fr_demo_ipc_timeout_binary_elapsed;
volatile uint32_t g_fr_demo_ipc_timeout_counting_elapsed;
volatile uint32_t g_fr_demo_ipc_timeout_mutex_elapsed;
volatile uint32_t g_fr_demo_ipc_timeout_queue_receive_elapsed;
volatile uint32_t g_fr_demo_ipc_timeout_queue_send_elapsed;
volatile uint32_t g_fr_demo_ipc_timeout_event_flags_elapsed;

fr_task_stack_info_t g_fr_demo_task_a_stack_diagnostics;
fr_task_stack_info_t g_fr_demo_task_b_stack_diagnostics;
fr_task_stack_info_t g_fr_demo_task_c_stack_diagnostics;
fr_task_stack_info_t g_fr_demo_monitor_stack_diagnostics;

volatile uint32_t g_fr_demo_stack_diagnostic_checks;
volatile uint32_t g_fr_demo_stack_diagnostic_errors;
volatile uint32_t g_fr_demo_stack_overflow_detected;

fr_kernel_invariant_report_t g_fr_demo_kernel_invariant_report;
volatile uint32_t g_fr_demo_kernel_invariant_checks;
volatile uint32_t g_fr_demo_kernel_invariant_errors;
volatile uint32_t g_fr_demo_assert_init_failures;

fr_trace_entry_t
    g_fr_demo_trace_snapshot[FR_DEMO_TRACE_SNAPSHOT_CAPACITY];
fr_trace_status_t g_fr_demo_trace_status;

volatile uint32_t g_fr_demo_trace_init_failures;
volatile uint32_t g_fr_demo_trace_checks;
volatile uint32_t g_fr_demo_trace_errors;
volatile uint32_t g_fr_demo_trace_snapshot_count;
volatile uint32_t g_fr_demo_trace_last_sequence;
volatile uint32_t g_fr_demo_trace_context_switch_events;
volatile uint32_t g_fr_demo_trace_block_events;
volatile uint32_t g_fr_demo_trace_unblock_events;
volatile uint32_t g_fr_demo_trace_timeout_events;

fr_uart_status_t g_fr_demo_uart_status;
fr_trace_entry_t
    g_fr_demo_monitor_trace[FR_DEMO_MONITOR_TRACE_CAPACITY];
static char g_fr_demo_uart_line[FR_DEMO_UART_LINE_CAPACITY];

volatile uint32_t g_fr_demo_uart_init_failures;
volatile uint32_t g_fr_demo_uart_errors;
volatile uint32_t g_fr_demo_uart_status_checks;
volatile uint32_t g_fr_demo_uart_commands;
volatile uint32_t g_fr_demo_uart_unknown_commands;
volatile uint32_t g_fr_demo_uart_help_reports;
volatile uint32_t g_fr_demo_uart_status_reports;
volatile uint32_t g_fr_demo_uart_trace_dumps;
volatile uint32_t g_fr_demo_uart_trace_clears;
volatile uint32_t g_fr_demo_uart_last_command;
volatile uint32_t g_fr_demo_uart_last_trace_count;
volatile uint32_t g_fr_demo_uart_last_trace_sequence;
volatile uint32_t g_fr_demo_stress_integration_errors;

_Static_assert((FR_DEMO_BOOTSTRAP_STACK_WORDS % 2u) == 0u, "Bootstrap stack size must preserve 8-byte alignment");
_Static_assert((FR_DEMO_TASK_A_STACK_WORDS % 2u) == 0u, "Task A stack size must preserve 8-byte alignment");
_Static_assert((FR_DEMO_TASK_B_STACK_WORDS % 2u) == 0u, "Task B stack size must preserve 8-byte alignment");
_Static_assert((FR_DEMO_TASK_C_STACK_WORDS % 2u) == 0u, "Task C stack size must preserve 8-byte alignment");
_Static_assert((FR_DEMO_MONITOR_TASK_STACK_WORDS % 2u) == 0u, "Monitor task stack size must preserve 8-byte alignment");
_Static_assert((FR_BOARD_RESET_CORE_CLOCK_HZ % FR_DEMO_TICK_HZ) == 0u, "SysTick frequency must divide core clock exactly");
_Static_assert(FR_DEMO_MONITOR_TASK_PRIORITY < FR_DEMO_TASK_A_PRIORITY, "Monitor task must have lower priority than Task A");
_Static_assert(FR_DEMO_TASK_A_PRIORITY < FR_DEMO_TASK_C_PRIORITY, "Task A must have lower priority than Task C");
_Static_assert(FR_DEMO_TASK_C_PRIORITY < FR_DEMO_TASK_B_PRIORITY, "Task C must have lower priority than Task B");

static void fr_demo_prepare_bootstrap_stack(void) {
    for (uint32_t i = 0u; i < FR_DEMO_BOOTSTRAP_STACK_WORDS; ++i) {
        g_fr_demo_bootstrap_stack[i] = FR_DEMO_STACK_PATTERN;
    }
}


static uint32_t fr_demo_advance_local_state(uint32_t state) {
    return (state * 1664525u) + 1013904223u;
}


static bool fr_demo_stack_info_is_valid(
    const fr_task_stack_info_t *info,
    uint32_t expected_size_words) {
    if ((info == NULL) ||
        (info->size_words != expected_size_words) ||
        (info->guard_words != FR_TASK_STACK_GUARD_WORDS) ||
        (info->usable_words !=
         (expected_size_words - FR_TASK_STACK_GUARD_WORDS)) ||
        (info->minimum_free_words > info->size_words) ||
        (info->peak_used_words > info->size_words) ||
        ((info->minimum_free_words + info->peak_used_words) !=
         info->size_words) ||
        !info->guard_intact ||
        !info->saved_sp_in_bounds ||
        !info->saved_sp_aligned ||
        info->overflow_detected) {
        return false;
    }

    const uint32_t expected_usable_free =
        (info->minimum_free_words > info->guard_words)
            ? (info->minimum_free_words - info->guard_words)
            : 0u;

    return info->minimum_usable_free_words == expected_usable_free;
}

static bool fr_demo_stack_watermark_monotonic(
    const fr_task_stack_info_t *previous,
    const fr_task_stack_info_t *current) {
    return (current->minimum_free_words <= previous->minimum_free_words) &&
           (current->peak_used_words >= previous->peak_used_words);
}

static void fr_demo_copy_stack_info(fr_task_stack_info_t *destination,
                                    const fr_task_stack_info_t *source) {
    destination->size_words = source->size_words;
    destination->guard_words = source->guard_words;
    destination->usable_words = source->usable_words;
    destination->minimum_free_words = source->minimum_free_words;
    destination->minimum_usable_free_words =
        source->minimum_usable_free_words;
    destination->peak_used_words = source->peak_used_words;

    destination->stack_base = source->stack_base;
    destination->stack_top = source->stack_top;
    destination->saved_sp = source->saved_sp;

    destination->guard_intact = source->guard_intact;
    destination->saved_sp_in_bounds = source->saved_sp_in_bounds;
    destination->saved_sp_aligned = source->saved_sp_aligned;
    destination->overflow_detected = source->overflow_detected;
}

static void fr_demo_update_stack_diagnostics(void) {
    fr_task_stack_info_t task_a;
    fr_task_stack_info_t task_b;
    fr_task_stack_info_t task_c;
    fr_task_stack_info_t monitor;

    bool valid =
        fr_task_stack_get_info(g_fr_demo_task_a, &task_a) &&
        fr_task_stack_get_info(g_fr_demo_task_b, &task_b) &&
        fr_task_stack_get_info(g_fr_demo_task_c, &task_c) &&
        fr_task_stack_get_info(g_fr_demo_monitor_task, &monitor);

    if (valid) {
        valid =
            fr_demo_stack_info_is_valid(
                &task_a, FR_DEMO_TASK_A_STACK_WORDS) &&
            fr_demo_stack_info_is_valid(
                &task_b, FR_DEMO_TASK_B_STACK_WORDS) &&
            fr_demo_stack_info_is_valid(
                &task_c, FR_DEMO_TASK_C_STACK_WORDS) &&
            fr_demo_stack_info_is_valid(
                &monitor, FR_DEMO_MONITOR_TASK_STACK_WORDS);
    }

    if (valid && (g_fr_demo_stack_diagnostic_checks != 0u)) {
        valid =
            fr_demo_stack_watermark_monotonic(
                &g_fr_demo_task_a_stack_diagnostics, &task_a) &&
            fr_demo_stack_watermark_monotonic(
                &g_fr_demo_task_b_stack_diagnostics, &task_b) &&
            fr_demo_stack_watermark_monotonic(
                &g_fr_demo_task_c_stack_diagnostics, &task_c) &&
            fr_demo_stack_watermark_monotonic(
                &g_fr_demo_monitor_stack_diagnostics, &monitor);
    }

    if (valid) {
        if (task_a.overflow_detected ||
            task_b.overflow_detected ||
            task_c.overflow_detected ||
            monitor.overflow_detected) {
            g_fr_demo_stack_overflow_detected = 1u;
        }

        fr_demo_copy_stack_info(
            &g_fr_demo_task_a_stack_diagnostics, &task_a);
        fr_demo_copy_stack_info(
            &g_fr_demo_task_b_stack_diagnostics, &task_b);
        fr_demo_copy_stack_info(
            &g_fr_demo_task_c_stack_diagnostics, &task_c);
        fr_demo_copy_stack_info(
            &g_fr_demo_monitor_stack_diagnostics, &monitor);
    } else {
        ++g_fr_demo_stack_diagnostic_errors;
    }

    ++g_fr_demo_stack_diagnostic_checks;
}

static void fr_demo_copy_kernel_invariant_report(
    fr_kernel_invariant_report_t *destination,
    const fr_kernel_invariant_report_t *source) {
    destination->task_count = source->task_count;
    destination->created_tasks = source->created_tasks;
    destination->ready_tasks = source->ready_tasks;
    destination->running_tasks = source->running_tasks;
    destination->blocked_tasks = source->blocked_tasks;
    destination->suspended_tasks = source->suspended_tasks;
    destination->current_task_id = source->current_task_id;
    destination->violation_mask = source->violation_mask;
    destination->first_bad_task_id = source->first_bad_task_id;
    destination->scheduler_running = source->scheduler_running;
}

static void fr_demo_update_kernel_invariants(void) {
    fr_kernel_invariant_report_t report;

    const bool valid = fr_kernel_check_invariants(&report);

    if (!valid ||
        !report.scheduler_running ||
        (report.task_count != g_fr_demo_task_count_snapshot) ||
        (report.created_tasks != 0u) ||
        (report.running_tasks != 1u) ||
        (report.current_task_id != g_fr_demo_task_b_info.id) ||
        (report.violation_mask != 0u)) {
        ++g_fr_demo_kernel_invariant_errors;
    }

    fr_demo_copy_kernel_invariant_report(
        &g_fr_demo_kernel_invariant_report,
        &report);

    ++g_fr_demo_kernel_invariant_checks;
}

static bool fr_demo_trace_event_is_valid(fr_trace_event_t event) {
    return (event >= FR_TRACE_EVENT_TASK_CREATE) &&
           (event <= FR_TRACE_EVENT_CONTEXT_SWITCH);
}

static void fr_demo_update_trace_snapshot(void) {
    bool valid = fr_trace_get_status(&g_fr_demo_trace_status);

    if (valid) {
        valid =
            g_fr_demo_trace_status.enabled &&
            (g_fr_demo_trace_status.capacity == FR_TRACE_BUFFER_CAPACITY) &&
            (g_fr_demo_trace_status.count <= FR_TRACE_BUFFER_CAPACITY) &&
            (g_fr_demo_trace_status.write_index < FR_TRACE_BUFFER_CAPACITY) &&
            (g_fr_demo_trace_status.next_sequence != 0u);
    }

    const uint32_t snapshot_count =
        fr_trace_snapshot(g_fr_demo_trace_snapshot,
                          FR_DEMO_TRACE_SNAPSHOT_CAPACITY);

    g_fr_demo_trace_snapshot_count = snapshot_count;

    uint32_t context_switch_events = 0u;
    uint32_t block_events = 0u;
    uint32_t unblock_events = 0u;
    uint32_t timeout_events = 0u;
    uint32_t previous_sequence = 0u;

    if ((snapshot_count == 0u) ||
        (snapshot_count > FR_DEMO_TRACE_SNAPSHOT_CAPACITY)) {
        valid = false;
    }

    for (uint32_t i = 0u; i < snapshot_count; ++i) {
        const fr_trace_entry_t *const entry =
            &g_fr_demo_trace_snapshot[i];

        if (!fr_demo_trace_event_is_valid(entry->event) ||
            ((i != 0u) &&
             (entry->sequence <= previous_sequence))) {
            valid = false;
        }

        previous_sequence = entry->sequence;

        switch (entry->event) {
            case FR_TRACE_EVENT_TASK_BLOCK:
                ++block_events;
                break;

            case FR_TRACE_EVENT_TASK_TIMEOUT:
                ++timeout_events;
                break;

            case FR_TRACE_EVENT_TASK_UNBLOCK:
                ++unblock_events;
                break;

            case FR_TRACE_EVENT_CONTEXT_SWITCH:
                ++context_switch_events;
                break;

            default:
                break;
        }
    }

    g_fr_demo_trace_context_switch_events = context_switch_events;
    g_fr_demo_trace_block_events = block_events;
    g_fr_demo_trace_unblock_events = unblock_events;
    g_fr_demo_trace_timeout_events = timeout_events;
    g_fr_demo_trace_last_sequence =
        (snapshot_count != 0u)
            ? g_fr_demo_trace_snapshot[snapshot_count - 1u].sequence
            : 0u;

    if (!valid) {
        ++g_fr_demo_trace_errors;
    }

    ++g_fr_demo_trace_checks;
}

static uint32_t fr_demo_text_length(const char *text) {
    if (text == NULL) {
        return 0u;
    }

    uint32_t length = 0u;

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

static bool fr_demo_uart_write_text(const char *text) {
    const uint32_t length = fr_demo_text_length(text);

    if ((length == 0u) ||
        !fr_uart_write((const uint8_t *)text, length)) {
        ++g_fr_demo_uart_errors;
        return false;
    }

    return true;
}

static bool fr_demo_line_append_char(uint32_t *length, char value) {
    if ((length == NULL) ||
        (*length >= FR_DEMO_UART_LINE_CAPACITY)) {
        return false;
    }

    g_fr_demo_uart_line[*length] = value;
    ++(*length);
    return true;
}

static bool fr_demo_line_append_text(uint32_t *length,
                                     const char *text) {
    if ((length == NULL) || (text == NULL)) {
        return false;
    }

    for (uint32_t i = 0u; text[i] != '\0'; ++i) {
        if (!fr_demo_line_append_char(length, text[i])) {
            return false;
        }
    }

    return true;
}

static bool fr_demo_line_append_u32(uint32_t *length,
                                    uint32_t value) {
    char digits[10];
    uint32_t digit_count = 0u;

    do {
        digits[digit_count] = (char)('0' + (value % 10u));
        ++digit_count;
        value /= 10u;
    } while ((value != 0u) && (digit_count < 10u));

    while (digit_count != 0u) {
        --digit_count;

        if (!fr_demo_line_append_char(length,
                                      digits[digit_count])) {
            return false;
        }
    }

    return true;
}

static const char *fr_demo_trace_event_name(fr_trace_event_t event) {
    switch (event) {
        case FR_TRACE_EVENT_TASK_CREATE:
            return "CREATE";

        case FR_TRACE_EVENT_SCHEDULER_START:
            return "START";

        case FR_TRACE_EVENT_TASK_BLOCK:
            return "BLOCK";

        case FR_TRACE_EVENT_TASK_TIMEOUT:
            return "TIMEOUT";

        case FR_TRACE_EVENT_TASK_UNBLOCK:
            return "UNBLOCK";

        case FR_TRACE_EVENT_CONTEXT_SWITCH:
            return "SWITCH";

        default:
            return "UNKNOWN";
    }
}

static bool fr_demo_uart_emit_trace_entry(
    const fr_trace_entry_t *entry) {
    if (entry == NULL) {
        return false;
    }

    uint32_t length = 0u;

    const bool built =
        fr_demo_line_append_char(&length, '#') &&
        fr_demo_line_append_u32(&length, entry->sequence) &&
        fr_demo_line_append_text(&length, " t=") &&
        fr_demo_line_append_u32(&length, entry->tick) &&
        fr_demo_line_append_char(&length, ' ') &&
        fr_demo_line_append_text(
            &length,
            fr_demo_trace_event_name(entry->event)) &&
        fr_demo_line_append_text(&length, " task=") &&
        fr_demo_line_append_u32(&length, entry->task_id) &&
        fr_demo_line_append_text(&length, " a0=") &&
        fr_demo_line_append_u32(&length, entry->arg0) &&
        fr_demo_line_append_text(&length, " a1=") &&
        fr_demo_line_append_u32(&length, entry->arg1) &&
        fr_demo_line_append_text(&length, "\r\n");

    if (!built ||
        !fr_uart_write((const uint8_t *)g_fr_demo_uart_line,
                       length)) {
        ++g_fr_demo_uart_errors;
        return false;
    }

    return true;
}

static void fr_demo_uart_emit_help(void) {
    if (fr_demo_uart_write_text(
            "Commands: h/? help, s status, t trace, x stress, c clear-trace\r\n")) {
        ++g_fr_demo_uart_help_reports;
    }
}

static void fr_demo_uart_emit_status(void) {
    fr_trace_status_t trace_status;

    if (!fr_uart_get_status(&g_fr_demo_uart_status) ||
        !fr_trace_get_status(&trace_status)) {
        ++g_fr_demo_uart_errors;
        return;
    }

    uint32_t length = 0u;

    const bool built =
        fr_demo_line_append_text(&length, "STATUS tick=") &&
        fr_demo_line_append_u32(&length, fr_tick_now()) &&
        fr_demo_line_append_text(&length, " tasks=") &&
        fr_demo_line_append_u32(&length, fr_task_count()) &&
        fr_demo_line_append_text(&length, " trace=") &&
        fr_demo_line_append_u32(&length, trace_status.count) &&
        fr_demo_line_append_char(&length, '/') &&
        fr_demo_line_append_u32(&length, trace_status.capacity) &&
        fr_demo_line_append_text(&length, " ovw=") &&
        fr_demo_line_append_u32(&length,
                                trace_status.overwritten_events) &&
        fr_demo_line_append_text(&length, " tx=") &&
        fr_demo_line_append_u32(&length,
                                g_fr_demo_uart_status.tx_bytes) &&
        fr_demo_line_append_text(&length, " rx=") &&
        fr_demo_line_append_u32(&length,
                                g_fr_demo_uart_status.rx_bytes) &&
        fr_demo_line_append_text(&length, " err=") &&
        fr_demo_line_append_u32(&length,
                                g_fr_demo_uart_status.rx_error_count) &&
        fr_demo_line_append_text(&length, "\r\n");

    if (!built ||
        !fr_uart_write((const uint8_t *)g_fr_demo_uart_line,
                       length)) {
        ++g_fr_demo_uart_errors;
        return;
    }

    ++g_fr_demo_uart_status_reports;
}

static void fr_demo_uart_emit_stress_status(void) {
    fr_demo_stress_status_t stress;

    if (!fr_demo_stress_get_status(&stress)) {
        ++g_fr_demo_uart_errors;
        return;
    }

    uint32_t length = 0u;

    const bool first_line =
        fr_demo_line_append_text(&length, "STRESS run=") &&
        fr_demo_line_append_u32(&length, stress.started ? 1u : 0u) &&
        fr_demo_line_append_text(&length, " sent=") &&
        fr_demo_line_append_u32(&length, stress.messages_sent) &&
        fr_demo_line_append_text(&length, " recv=") &&
        fr_demo_line_append_u32(&length, stress.messages_received) &&
        fr_demo_line_append_text(&length, " q=") &&
        fr_demo_line_append_u32(&length, stress.queue_depth) &&
        fr_demo_line_append_char(&length, '/') &&
        fr_demo_line_append_u32(&length, stress.queue_capacity) &&
        fr_demo_line_append_text(&length, " sync=") &&
        fr_demo_line_append_u32(&length, stress.sync_completions) &&
        fr_demo_line_append_char(&length, '/') &&
        fr_demo_line_append_u32(&length, stress.sync_requests) &&
        fr_demo_line_append_text(&length, " mutex=") &&
        fr_demo_line_append_u32(&length, stress.mutex_acquisitions) &&
        fr_demo_line_append_text(&length, " ack=") &&
        fr_demo_line_append_u32(&length, stress.ack_successes) &&
        fr_demo_line_append_text(&length, " err=") &&
        fr_demo_line_append_u32(&length, stress.errors) &&
        fr_demo_line_append_text(&length, "\r\n");

    if (!first_line ||
        !fr_uart_write((const uint8_t *)g_fr_demo_uart_line, length)) {
        ++g_fr_demo_uart_errors;
        return;
    }

    length = 0u;

    const bool second_line =
        fr_demo_line_append_text(&length, "STRESSCHK order=") &&
        fr_demo_line_append_u32(&length, stress.order_errors) &&
        fr_demo_line_append_text(&length, " crc=") &&
        fr_demo_line_append_u32(&length, stress.checksum_errors) &&
        fr_demo_line_append_text(&length, " laterr=") &&
        fr_demo_line_append_u32(&length, stress.latency_errors) &&
        fr_demo_line_append_text(&length, " latmax=") &&
        fr_demo_line_append_u32(&length, stress.max_latency_ticks) &&
        fr_demo_line_append_text(&length, " qtx=") &&
        fr_demo_line_append_u32(&length, stress.queue_send_timeouts) &&
        fr_demo_line_append_text(&length, " qrx=") &&
        fr_demo_line_append_u32(&length, stress.queue_receive_timeouts) &&
        fr_demo_line_append_text(&length, " ackto=") &&
        fr_demo_line_append_u32(&length, stress.ack_timeouts) &&
        fr_demo_line_append_text(&length, " pi=") &&
        fr_demo_line_append_u32(&length, stress.priority_errors) &&
        fr_demo_line_append_text(&length, " timing=") &&
        fr_demo_line_append_u32(&length, stress.timing_errors) &&
        fr_demo_line_append_text(&length, " stack=") &&
        fr_demo_line_append_u32(&length, stress.stack_errors) &&
        fr_demo_line_append_text(&length, "\r\n");

    if (!second_line ||
        !fr_uart_write((const uint8_t *)g_fr_demo_uart_line, length)) {
        ++g_fr_demo_uart_errors;
    }
}

static void fr_demo_uart_dump_trace(void) {
    const uint32_t count =
        fr_trace_snapshot(g_fr_demo_monitor_trace,
                          FR_DEMO_MONITOR_TRACE_CAPACITY);

    g_fr_demo_uart_last_trace_count = count;

    if (!fr_demo_uart_write_text("TRACE newest events:\r\n")) {
        return;
    }

    for (uint32_t i = 0u; i < count; ++i) {
        if (!fr_demo_uart_emit_trace_entry(
                &g_fr_demo_monitor_trace[i])) {
            return;
        }
    }

    g_fr_demo_uart_last_trace_sequence =
        (count != 0u)
            ? g_fr_demo_monitor_trace[count - 1u].sequence
            : 0u;

    ++g_fr_demo_uart_trace_dumps;
}

static void fr_demo_uart_process_command(uint8_t command) {
    if ((command == '\r') || (command == '\n')) {
        return;
    }

    g_fr_demo_uart_last_command = command;
    ++g_fr_demo_uart_commands;

    switch (command) {
        case 'h':
        case '?':
            fr_demo_uart_emit_help();
            break;

        case 's':
            fr_demo_uart_emit_status();
            break;

        case 't':
            fr_demo_uart_dump_trace();
            break;

        case 'x':
            fr_demo_uart_emit_stress_status();
            break;

        case 'c':
            fr_trace_clear();
            ++g_fr_demo_uart_trace_clears;
            (void)fr_demo_uart_write_text("OK trace cleared\r\n");
            break;

        default:
            ++g_fr_demo_uart_unknown_commands;
            (void)fr_demo_uart_write_text(
                "ERR unknown command; use h\r\n");
            break;
    }
}

static bool fr_demo_execution_context_is_valid(fr_task_handle_t expected_task,
                                                const fr_task_info_t *task_info) {
    volatile fr_arch_stack_state_t state;

    fr_arch_capture_stack_state(&state);

    if (fr_scheduler_current_task() != expected_task) {
        return false;
    }

    if (state.ipsr != 0u) {
        return false;
    }

    if ((state.control & 0x2u) == 0u) {
        return false;
    }

    if (state.sp != state.psp) {
        return false;
    }

    const uintptr_t stack_pointer = (uintptr_t)state.psp;

    return (stack_pointer >= task_info->stack_base) &&
           (stack_pointer <= task_info->stack_top);
}

static bool fr_demo_stack_probe_is_valid(const volatile uint32_t *probe, const uint32_t *expected) {
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (probe[i] != expected[i]) {
            return false;
        }
    }

    return true;
}

static void fr_demo_validate_preempted_task(fr_task_handle_t task,
                                            const fr_task_info_t *task_info,
                                            const volatile uint32_t *stack_probe,
                                            const uint32_t *expected_stack_probe,
                                            uint32_t local_state,
                                            volatile fr_demo_preemption_stats_t *stats) {
    if (!fr_demo_execution_context_is_valid(task, task_info)) {
        ++stats->execution_errors;
    }

    if (!fr_demo_stack_probe_is_valid(stack_probe, expected_stack_probe)) {
        ++stats->stack_errors;
    }

    if ((stats->validations != 0u) && (stats->local_state_snapshot == local_state)) {
        ++stats->local_state_errors;
    }

    stats->local_state_snapshot = local_state;
    ++stats->validations;
}

static uint32_t fr_demo_test_timeout_math(void) {
    uint32_t failures = 0u;

    if (fr_tick_deadline_reached(99u, 100u)) {
        ++failures;
    }

    if (!fr_tick_deadline_reached(100u, 100u)) {
        ++failures;
    }

    if (fr_tick_deadline_reached(0xFFFFFFFEu, 0x00000003u)) {
        ++failures;
    }

    if (fr_tick_deadline_reached(0x00000002u, 0x00000003u)) {
        ++failures;
    }

    if (!fr_tick_deadline_reached(0x00000003u, 0x00000003u)) {
        ++failures;
    }

    if (!fr_tick_deadline_reached(0x00000004u, 0x00000003u)) {
        ++failures;
    }

    return failures;
}

static bool fr_demo_timeout_elapsed_at_least(
    fr_tick_t start,
    uint32_t minimum_ticks,
    volatile uint32_t *elapsed_out) {
    if (elapsed_out == NULL) {
        return false;
    }

    const uint32_t elapsed =
        fr_tick_elapsed(start, fr_tick_now());

    *elapsed_out = elapsed;

    return elapsed >= minimum_ticks;
}


static void fr_demo_run_semaphore_probe(void) {
    if (!fr_binary_semaphore_take(&g_fr_demo_semaphore_probe, 0u)) {
        ++g_fr_demo_semaphore_probe_failures;
    }

    if (!fr_binary_semaphore_give(&g_fr_demo_semaphore_probe)) {
        ++g_fr_demo_semaphore_probe_failures;
    }

    if (fr_binary_semaphore_give(&g_fr_demo_semaphore_probe)) {
        ++g_fr_demo_semaphore_probe_failures;
    }

    if (!fr_binary_semaphore_take(&g_fr_demo_semaphore_probe, 0u)) {
        ++g_fr_demo_semaphore_probe_failures;
    }

    if (fr_binary_semaphore_take(&g_fr_demo_semaphore_probe, 0u)) {
        ++g_fr_demo_semaphore_probe_failures;
    }
}

static void fr_demo_run_counting_semaphore_probe(void) {
    if ((g_fr_demo_counting_probe.count != 2u) ||
        (g_fr_demo_counting_probe.max_count != 3u)) {
        ++g_fr_demo_counting_probe_failures;
    }

    if (!fr_counting_semaphore_take(&g_fr_demo_counting_probe, 0u) ||
        (g_fr_demo_counting_probe.count != 1u)) {
        ++g_fr_demo_counting_probe_failures;
    }

    if (!fr_counting_semaphore_give(&g_fr_demo_counting_probe) ||
        (g_fr_demo_counting_probe.count != 2u)) {
        ++g_fr_demo_counting_probe_failures;
    }

    if (!fr_counting_semaphore_give(&g_fr_demo_counting_probe) ||
        (g_fr_demo_counting_probe.count != 3u)) {
        ++g_fr_demo_counting_probe_failures;
    }

    if (fr_counting_semaphore_give(&g_fr_demo_counting_probe) ||
        (g_fr_demo_counting_probe.count != 3u)) {
        ++g_fr_demo_counting_probe_failures;
    }

    if (!fr_counting_semaphore_take(&g_fr_demo_counting_probe, 0u) ||
        (g_fr_demo_counting_probe.count != 2u)) {
        ++g_fr_demo_counting_probe_failures;
    }

    if (!fr_counting_semaphore_take(&g_fr_demo_counting_probe, 0u) ||
        (g_fr_demo_counting_probe.count != 1u)) {
        ++g_fr_demo_counting_probe_failures;
    }

    if (!fr_counting_semaphore_take(&g_fr_demo_counting_probe, 0u) ||
        (g_fr_demo_counting_probe.count != 0u)) {
        ++g_fr_demo_counting_probe_failures;
    }

    if (fr_counting_semaphore_take(&g_fr_demo_counting_probe, 0u) ||
        (g_fr_demo_counting_probe.count != 0u)) {
        ++g_fr_demo_counting_probe_failures;
    }
}

static void fr_demo_run_ipc_timeout_probe(void) {
    fr_tick_t start = fr_tick_now();

    if (!fr_binary_semaphore_take(
            &g_fr_demo_ipc_timeout_binary,
            FR_DEMO_IPC_TIMEOUT_TICKS)) {
        ++g_fr_demo_ipc_timeout_binary_observed;

        if (fr_demo_timeout_elapsed_at_least(
                start,
                FR_DEMO_IPC_TIMEOUT_TICKS,
                &g_fr_demo_ipc_timeout_binary_elapsed)) {
            g_fr_demo_ipc_timeout_binary_verified = 1u;
        } else {
            ++g_fr_demo_ipc_timeout_errors;
        }
    } else {
        ++g_fr_demo_ipc_timeout_errors;
    }

    start = fr_tick_now();

    if (!fr_counting_semaphore_take(
            &g_fr_demo_ipc_timeout_counting,
            FR_DEMO_IPC_TIMEOUT_TICKS)) {
        ++g_fr_demo_ipc_timeout_counting_observed;

        if (fr_demo_timeout_elapsed_at_least(
                start,
                FR_DEMO_IPC_TIMEOUT_TICKS,
                &g_fr_demo_ipc_timeout_counting_elapsed)) {
            g_fr_demo_ipc_timeout_counting_verified = 1u;
        } else {
            ++g_fr_demo_ipc_timeout_errors;
        }
    } else {
        ++g_fr_demo_ipc_timeout_errors;
    }
}

static void fr_demo_run_queue_probe(void) {
    uint32_t value = 0u;

    const uint32_t item_a = 0x11111111u;
    const uint32_t item_b = 0x22222222u;
    const uint32_t item_c = 0x33333333u;
    const uint32_t item_d = 0x44444444u;

    if (fr_queue_receive(&g_fr_demo_queue_probe, &value)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (!fr_queue_send(&g_fr_demo_queue_probe, &item_a)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (!fr_queue_send(&g_fr_demo_queue_probe, &item_b)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (!fr_queue_send(&g_fr_demo_queue_probe, &item_c)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (fr_queue_send(&g_fr_demo_queue_probe, &item_d)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (!fr_queue_receive(&g_fr_demo_queue_probe, &value) ||
        (value != item_a)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (!fr_queue_send(&g_fr_demo_queue_probe, &item_d)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (!fr_queue_receive(&g_fr_demo_queue_probe, &value) ||
        (value != item_b)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (!fr_queue_receive(&g_fr_demo_queue_probe, &value) ||
        (value != item_c)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (!fr_queue_receive(&g_fr_demo_queue_probe, &value) ||
        (value != item_d)) {
        ++g_fr_demo_queue_probe_failures;
    }

    if (fr_queue_receive(&g_fr_demo_queue_probe, &value)) {
        ++g_fr_demo_queue_probe_failures;
    }
}

static void fr_demo_task_a_entry(void *argument) {
    volatile uint32_t local_state = FR_DEMO_TASK_A_LOCAL_STATE_SEED;

    volatile uint32_t stack_probe[4] = {
        0xA0A0A0A0u,
        0xA1A1A1A1u,
        0xA2A2A2A2u,
        0xA3A3A3A3u
    };

    ++g_fr_demo_task_a_entry_count;
    g_fr_demo_task_a_started = 1u;

    if (argument != NULL) {
        g_fr_demo_task_a_argument_value = *(const uint32_t *)argument;
    }

    if (fr_mutex_lock(&g_fr_demo_inversion_mutex, 0u)) {
        ++g_fr_demo_inversion_low_lock_successes;
        g_fr_demo_inversion_low_lock_tick = fr_tick_now();
    } else {
        ++g_fr_demo_inversion_errors;
    }

    if (!fr_binary_semaphore_give(&g_fr_demo_inversion_high_gate)) {
        ++g_fr_demo_inversion_errors;
    }

    /*
     * Task B preempts A here, blocks on the inversion mutex, and donates
     * its higher priority to A. When A resumes, its effective priority
     * should be 9 while its configured base priority remains 3.
     */
    if (!fr_binary_semaphore_give(&g_fr_demo_inversion_medium_gate)) {
        ++g_fr_demo_inversion_errors;
    }

    fr_task_info_t inheritance_info;

    if (!fr_task_get_info(g_fr_demo_task_a, &inheritance_info)) {
        ++g_fr_demo_inversion_errors;
    } else {
        g_fr_demo_inheritance_low_effective_priority =
            inheritance_info.priority;
        g_fr_demo_inheritance_low_base_priority =
            inheritance_info.base_priority;

        if ((inheritance_info.priority == FR_DEMO_TASK_B_PRIORITY) &&
            (inheritance_info.base_priority == FR_DEMO_TASK_A_PRIORITY)) {
            g_fr_demo_inheritance_owner_boosted = 1u;
        } else {
            ++g_fr_demo_inversion_errors;
        }
    }

    g_fr_demo_inversion_low_unlock_tick = fr_tick_now();

    if (fr_mutex_unlock(&g_fr_demo_inversion_mutex)) {
        ++g_fr_demo_inversion_low_unlock_successes;
    } else {
        ++g_fr_demo_inversion_errors;
    }

    /*
     * Task B may preempt A inside fr_mutex_unlock(). When A eventually
     * resumes, the inherited priority must have been removed.
     */
    if (!fr_task_get_info(g_fr_demo_task_a, &inheritance_info)) {
        ++g_fr_demo_inversion_errors;
    } else {
        g_fr_demo_inheritance_low_priority_after_unlock =
            inheritance_info.priority;

        if ((inheritance_info.priority == FR_DEMO_TASK_A_PRIORITY) &&
            (inheritance_info.base_priority == FR_DEMO_TASK_A_PRIORITY)) {
            g_fr_demo_inheritance_owner_restored = 1u;
        } else {
            ++g_fr_demo_inversion_errors;
        }
    }

    if (fr_mutex_lock(&g_fr_demo_mutex, 0u)) {
        ++g_fr_demo_mutex_a_lock_successes;
    } else {
        ++g_fr_demo_mutex_errors;
    }

    if (fr_mutex_lock(&g_fr_demo_mutex_timeout_probe, 0u)) {
        ++g_fr_demo_mutex_a_lock_successes;
    } else {
        ++g_fr_demo_mutex_errors;
    }

    if (fr_mutex_lock(&g_fr_demo_mutex, 0u)) {
        ++g_fr_demo_mutex_errors;
    } else {
        ++g_fr_demo_mutex_a_recursive_rejections;
    }

    if (!fr_binary_semaphore_give(&g_fr_demo_mutex_start_gate)) {
        ++g_fr_demo_inversion_errors;
    }

    if (!fr_task_sleep(FR_DEMO_MUTEX_HOLD_TICKS)) {
        ++g_fr_demo_mutex_errors;
    }

    if (fr_mutex_unlock(&g_fr_demo_mutex_timeout_probe)) {
        ++g_fr_demo_mutex_a_unlock_successes;
    } else {
        ++g_fr_demo_mutex_errors;
    }

    if (fr_mutex_unlock(&g_fr_demo_mutex)) {
        ++g_fr_demo_mutex_a_unlock_successes;
    } else {
        ++g_fr_demo_mutex_errors;
    }

    while (1) {

        if ((g_fr_demo_blocking_queue_receive_waiting != 0u) &&
            (g_fr_demo_blocking_queue_receive_released == 0u)) {
            const uint32_t value = FR_DEMO_BLOCKING_QUEUE_VALUE_A;

            if (fr_queue_send(&g_fr_demo_blocking_queue,
                            &value)) {
                g_fr_demo_blocking_queue_receive_released = 1u;
                ++g_fr_demo_blocking_queue_release_send_successes;
            } else {
                ++g_fr_demo_blocking_queue_errors;
            }
        }

        if ((g_fr_demo_blocking_queue_send_waiting != 0u) &&
            (g_fr_demo_blocking_queue_send_released == 0u)) {
            uint32_t value = 0u;

            if (fr_queue_receive(&g_fr_demo_blocking_queue,
                                 &value)) {
                g_fr_demo_blocking_queue_send_released = 1u;
                ++g_fr_demo_blocking_queue_release_receive_successes;

                if (value != FR_DEMO_BLOCKING_QUEUE_VALUE_B1) {
                    ++g_fr_demo_blocking_queue_data_errors;
                }
            } else {
                ++g_fr_demo_blocking_queue_errors;
            }
        }

        if ((g_fr_demo_event_flags_any_requested != 0u) &&
            (g_fr_demo_event_flags_any_released == 0u)) {
            if (fr_event_flags_set(
                    &g_fr_demo_event_flags,
                    FR_DEMO_EVENT_FLAG_A)) {
                ++g_fr_demo_event_flags_set_successes;
                g_fr_demo_event_flags_any_released = 1u;
            } else {
                ++g_fr_demo_event_flags_errors;
            }
        }

        if ((g_fr_demo_event_flags_all_requested != 0u) &&
            (g_fr_demo_event_flags_all_stage == 0u)) {
            if (fr_event_flags_set(
                    &g_fr_demo_event_flags,
                    FR_DEMO_EVENT_FLAG_A)) {
                ++g_fr_demo_event_flags_set_successes;
                g_fr_demo_event_flags_all_stage = 1u;
            } else {
                ++g_fr_demo_event_flags_errors;
            }
        } else if ((g_fr_demo_event_flags_all_requested != 0u) &&
                   (g_fr_demo_event_flags_all_stage == 1u)) {
            if (fr_event_flags_set(
                    &g_fr_demo_event_flags,
                    FR_DEMO_EVENT_FLAG_B)) {
                ++g_fr_demo_event_flags_set_successes;
                g_fr_demo_event_flags_all_stage = 2u;
            } else {
                ++g_fr_demo_event_flags_errors;
            }
        }

        ++g_fr_demo_task_a_iterations;

        local_state = fr_demo_advance_local_state(local_state);

        if ((g_fr_demo_task_a_iterations & FR_DEMO_VALIDATION_MASK) == 0u) {
            fr_demo_validate_preempted_task(g_fr_demo_task_a,
                                            &g_fr_demo_task_a_info,
                                            stack_probe,
                                            g_fr_demo_task_a_stack_pattern,
                                            local_state,
                                            &g_fr_demo_task_a_preemption);
        }
        const fr_tick_t sleep_start = fr_tick_now();

        ++g_fr_demo_task_a_sleep_calls;

        if (fr_task_sleep(FR_DEMO_TASK_A_SLEEP_TICKS)) {
            ++g_fr_demo_task_a_wakeups;

            g_fr_demo_task_a_last_sleep_elapsed =
                fr_tick_elapsed(sleep_start, fr_tick_now());

            fr_demo_queue_message_t message = {
                .sequence = g_fr_demo_queue_next_sequence + 1u,
                .produced_tick = fr_tick_now()
            };

            if (fr_queue_send(&g_fr_demo_queue, &message)) {
                g_fr_demo_queue_next_sequence = message.sequence;
                g_fr_demo_queue_last_sent_sequence = message.sequence;
                g_fr_demo_queue_last_produced_tick = message.produced_tick;

                ++g_fr_demo_queue_send_successes;
            } else {
                ++g_fr_demo_queue_send_full;
            }

            if (g_fr_demo_task_a_wakeups == 1u) {
                if (fr_counting_semaphore_give(
                        &g_fr_demo_counting_semaphore)) {
                    ++g_fr_demo_counting_give_successes;
                }
            }

            if (g_fr_demo_task_a_last_sleep_elapsed < FR_DEMO_TASK_A_SLEEP_TICKS) {
                ++g_fr_demo_task_a_sleep_errors;
            }

            if ((g_fr_demo_task_a_wakeups % 3u) == 0u) {
                if (fr_binary_semaphore_give(&g_fr_demo_binary_semaphore)) {
                    ++g_fr_demo_semaphore_give_successes;
                } else {
                    ++g_fr_demo_semaphore_give_full;
                }
            }

        } else {
            ++g_fr_demo_task_a_sleep_errors;
        }
    }
}

static void fr_demo_task_b_entry(void *argument) {
    volatile uint32_t local_state = FR_DEMO_TASK_B_LOCAL_STATE_SEED;

    volatile uint32_t stack_probe[4] = {
        0xB0B0B0B0u,
        0xB1B1B1B1u,
        0xB2B2B2B2u,
        0xB3B3B3B3u
    };

    ++g_fr_demo_task_b_entry_count;
    g_fr_demo_task_b_started = 1u;

    if (argument != NULL) {
        g_fr_demo_task_b_argument_value = *(const uint32_t *)argument;
    }

    fr_tick_t last_toggle_tick = fr_tick_now();

    if (!fr_binary_semaphore_take(
            &g_fr_demo_inversion_high_gate,
            FR_BINARY_SEMAPHORE_WAIT_FOREVER)) {
        ++g_fr_demo_inversion_errors;
    }

    g_fr_demo_inversion_high_wait_start_tick = fr_tick_now();
    ++g_fr_demo_inversion_high_wait_started;

    if (fr_mutex_lock(&g_fr_demo_inversion_mutex,
                      FR_MUTEX_WAIT_FOREVER)) {
        ++g_fr_demo_inversion_high_lock_successes;

        g_fr_demo_inversion_high_acquire_tick = fr_tick_now();

        g_fr_demo_inversion_high_wait_elapsed =
            fr_tick_elapsed(g_fr_demo_inversion_high_wait_start_tick,
                            g_fr_demo_inversion_high_acquire_tick);

        if (g_fr_demo_inversion_mutex.owner != g_fr_demo_task_b) {
            ++g_fr_demo_inversion_errors;
        }

        /*
         * With priority inheritance, B must acquire the mutex before the
         * medium-priority workload is allowed to run.
         */
        if ((g_fr_demo_inversion_medium_started == 0u) &&
            (g_fr_demo_inversion_medium_completed == 0u)) {
            g_fr_demo_inheritance_high_before_medium = 1u;
        } else {
            ++g_fr_demo_inversion_errors;
        }

        if (fr_mutex_unlock(&g_fr_demo_inversion_mutex)) {
            ++g_fr_demo_inversion_high_unlock_successes;
        } else {
            ++g_fr_demo_inversion_errors;
        }
    } else {
        ++g_fr_demo_inversion_errors;
    }

    /*
     * Wait here until Task A has prepared the original mutex test.
     */
    if (!fr_binary_semaphore_take(
            &g_fr_demo_mutex_start_gate,
            FR_BINARY_SEMAPHORE_WAIT_FOREVER)) {
        ++g_fr_demo_inversion_errors;
    }

    const fr_tick_t mutex_timeout_start = fr_tick_now();

    if (!fr_mutex_lock(&g_fr_demo_mutex_timeout_probe,
                        FR_DEMO_MUTEX_TIMEOUT_TICKS)) {
        ++g_fr_demo_mutex_b_timeout_observed;

        if (fr_demo_timeout_elapsed_at_least(
                mutex_timeout_start,
                FR_DEMO_MUTEX_TIMEOUT_TICKS,
                &g_fr_demo_ipc_timeout_mutex_elapsed)) {
            g_fr_demo_ipc_timeout_mutex_verified = 1u;
        } else {
            ++g_fr_demo_ipc_timeout_errors;
        }
    } else {
        ++g_fr_demo_mutex_errors;

        if (!fr_mutex_unlock(&g_fr_demo_mutex_timeout_probe)) {
            ++g_fr_demo_mutex_errors;
        }
    }

    if (fr_mutex_unlock(&g_fr_demo_mutex)) {
        ++g_fr_demo_mutex_errors;
    } else {
        ++g_fr_demo_mutex_b_non_owner_rejections;
    }

    if (fr_mutex_lock(&g_fr_demo_mutex, FR_MUTEX_WAIT_FOREVER)) {
        ++g_fr_demo_mutex_b_lock_successes;

        if (g_fr_demo_mutex.owner != g_fr_demo_task_b) {
            ++g_fr_demo_mutex_handoff_errors;
        }

        if (fr_mutex_unlock(&g_fr_demo_mutex)) {
            ++g_fr_demo_mutex_b_unlock_successes;
        } else {
            ++g_fr_demo_mutex_errors;
        }
    } else {
        ++g_fr_demo_mutex_errors;
    }

    uint32_t blocking_value = 0u;

    /*
    * 1. Empty queue + finite receive timeout.
    */
    const fr_tick_t queue_receive_timeout_start =
        fr_tick_now();

    if (!fr_queue_receive_wait(
            &g_fr_demo_blocking_queue,
            &blocking_value,
            FR_DEMO_BLOCKING_QUEUE_TIMEOUT_TICKS)) {
        ++g_fr_demo_blocking_queue_receive_timeout_observed;

        if (fr_demo_timeout_elapsed_at_least(
                queue_receive_timeout_start,
                FR_DEMO_BLOCKING_QUEUE_TIMEOUT_TICKS,
                &g_fr_demo_ipc_timeout_queue_receive_elapsed)) {
            g_fr_demo_ipc_timeout_queue_receive_verified = 1u;
        } else {
            ++g_fr_demo_ipc_timeout_errors;
        }
    } else {
        ++g_fr_demo_blocking_queue_errors;
    }

    /*
    * 2. Empty queue + infinite receive.
    * Task A will send one item and wake B.
    */
    g_fr_demo_blocking_queue_receive_waiting = 1u;

    if (fr_queue_receive_wait(
            &g_fr_demo_blocking_queue,
            &blocking_value,
            FR_QUEUE_WAIT_FOREVER)) {
        ++g_fr_demo_blocking_queue_receive_wait_successes;

        if (blocking_value !=
            FR_DEMO_BLOCKING_QUEUE_VALUE_A) {
            ++g_fr_demo_blocking_queue_data_errors;
        }
    } else {
        ++g_fr_demo_blocking_queue_errors;
    }

    g_fr_demo_blocking_queue_receive_waiting = 0u;

    /*
    * 3. Fill the capacity-1 queue.
    */
    const uint32_t first_send = FR_DEMO_BLOCKING_QUEUE_VALUE_B1;

    const uint32_t second_send = FR_DEMO_BLOCKING_QUEUE_VALUE_B2;

    if (!fr_queue_send(&g_fr_demo_blocking_queue,
                        &first_send)) {
        ++g_fr_demo_blocking_queue_errors;
    }

    /*
    * 4. Full queue + finite send timeout.
    */
    const fr_tick_t queue_send_timeout_start =
        fr_tick_now();

    if (!fr_queue_send_wait(
            &g_fr_demo_blocking_queue,
            &second_send,
            FR_DEMO_BLOCKING_QUEUE_TIMEOUT_TICKS)) {
        ++g_fr_demo_blocking_queue_send_timeout_observed;

        if (fr_demo_timeout_elapsed_at_least(
                queue_send_timeout_start,
                FR_DEMO_BLOCKING_QUEUE_TIMEOUT_TICKS,
                &g_fr_demo_ipc_timeout_queue_send_elapsed)) {
            g_fr_demo_ipc_timeout_queue_send_verified = 1u;
        } else {
            ++g_fr_demo_ipc_timeout_errors;
        }
    } else {
        ++g_fr_demo_blocking_queue_errors;
    }

    /*
    * 5. Full queue + infinite send.
    * Task A will receive B1 and wake B.
    */
    g_fr_demo_blocking_queue_send_waiting = 1u;

    if (fr_queue_send_wait(
            &g_fr_demo_blocking_queue,
            &second_send,
            FR_QUEUE_WAIT_FOREVER)) {
        ++g_fr_demo_blocking_queue_send_wait_successes;
    } else {
        ++g_fr_demo_blocking_queue_errors;
    }

    g_fr_demo_blocking_queue_send_waiting = 0u;

    /*
    * 6. B2 must now be the only queued item.
    */
    blocking_value = 0u;

    if (!fr_queue_receive(&g_fr_demo_blocking_queue,
                          &blocking_value)) {
        ++g_fr_demo_blocking_queue_errors;
    } else if (blocking_value !=
               FR_DEMO_BLOCKING_QUEUE_VALUE_B2) {
        ++g_fr_demo_blocking_queue_data_errors;
    }

    uint32_t matched_flags = 0u;
    uint32_t flags_snapshot = 0u;

    /*
     * 1. Finite timeout: A is not set.
     */
    const fr_tick_t event_flags_timeout_start =
        fr_tick_now();

    if (!fr_event_flags_wait(
            &g_fr_demo_event_flags,
            FR_DEMO_EVENT_FLAG_A,
            true,
            false,
            FR_DEMO_EVENT_FLAGS_TIMEOUT_TICKS,
            &matched_flags)) {
        ++g_fr_demo_event_flags_timeout_observed;

        if (fr_demo_timeout_elapsed_at_least(
                event_flags_timeout_start,
                FR_DEMO_EVENT_FLAGS_TIMEOUT_TICKS,
                &g_fr_demo_ipc_timeout_event_flags_elapsed)) {
            g_fr_demo_ipc_timeout_event_flags_verified = 1u;
        } else {
            ++g_fr_demo_ipc_timeout_errors;
        }

        if (matched_flags != 0u) {
            ++g_fr_demo_event_flags_data_errors;
        }
    } else {
        ++g_fr_demo_event_flags_errors;
    }

    /*
     * 2. Wait for ANY of A or B.
     * Task A will set A.
     */
    g_fr_demo_event_flags_any_requested = 1u;
    matched_flags = 0u;

    if (fr_event_flags_wait(
            &g_fr_demo_event_flags,
            FR_DEMO_EVENT_FLAGS_AB,
            false,
            false,
            FR_EVENT_FLAGS_WAIT_FOREVER,
            &matched_flags)) {
        ++g_fr_demo_event_flags_wait_any_successes;
        g_fr_demo_event_flags_any_match = matched_flags;

        if (matched_flags != FR_DEMO_EVENT_FLAG_A) {
            ++g_fr_demo_event_flags_data_errors;
        }
    } else {
        ++g_fr_demo_event_flags_errors;
    }

    g_fr_demo_event_flags_any_requested = 0u;

    /*
     * clear_on_exit was false, so A must still be set.
     */
    if (!fr_event_flags_get(
            &g_fr_demo_event_flags,
            &flags_snapshot) ||
        (flags_snapshot != FR_DEMO_EVENT_FLAG_A)) {
        ++g_fr_demo_event_flags_data_errors;
    }

    /*
     * Explicitly clear A before the wait-all test.
     */
    if (fr_event_flags_clear(
            &g_fr_demo_event_flags,
            FR_DEMO_EVENT_FLAG_A)) {
        ++g_fr_demo_event_flags_clear_successes;
    } else {
        ++g_fr_demo_event_flags_errors;
    }

    if (!fr_event_flags_get(
            &g_fr_demo_event_flags,
            &flags_snapshot) ||
        (flags_snapshot != 0u)) {
        ++g_fr_demo_event_flags_data_errors;
    }

    /*
     * 3. Wait for ALL: A and B.
     *
     * Task A first sets A. B must remain blocked.
     * On the next A iteration, B is set and the condition becomes true.
     *
     * clear_on_exit is true, so both bits must be clear after wake-up.
     */
    g_fr_demo_event_flags_all_requested = 1u;
    g_fr_demo_event_flags_all_stage = 0u;
    matched_flags = 0u;

    if (fr_event_flags_wait(
            &g_fr_demo_event_flags,
            FR_DEMO_EVENT_FLAGS_AB,
            true,
            true,
            FR_EVENT_FLAGS_WAIT_FOREVER,
            &matched_flags)) {
        ++g_fr_demo_event_flags_wait_all_successes;
        g_fr_demo_event_flags_all_match = matched_flags;

        if (matched_flags != FR_DEMO_EVENT_FLAGS_AB) {
            ++g_fr_demo_event_flags_data_errors;
        }
    } else {
        ++g_fr_demo_event_flags_errors;
    }

    g_fr_demo_event_flags_all_requested = 0u;

    if (!fr_event_flags_get(
            &g_fr_demo_event_flags,
            &flags_snapshot)) {
        ++g_fr_demo_event_flags_errors;
    } else {
        g_fr_demo_event_flags_final_bits = flags_snapshot;

        if (flags_snapshot != 0u) {
            ++g_fr_demo_event_flags_data_errors;
        }
    }

    fr_demo_run_ipc_timeout_probe();

    fr_demo_run_queue_probe();

    fr_demo_run_counting_semaphore_probe();

    if (fr_counting_semaphore_take(
            &g_fr_demo_counting_semaphore,
            FR_COUNTING_SEMAPHORE_WAIT_FOREVER)) {
        ++g_fr_demo_counting_wait_successes;
    } else {
        ++g_fr_demo_counting_wait_errors;
    }

    fr_demo_run_semaphore_probe();

    if (!fr_binary_semaphore_take(&g_fr_demo_binary_semaphore, 0u)) {
        ++g_fr_demo_semaphore_initial_empty_checks;
    }

    if (!fr_binary_semaphore_take(&g_fr_demo_binary_semaphore, 2u)) {
        ++g_fr_demo_semaphore_initial_timeouts;
    }

    if (fr_binary_semaphore_take(&g_fr_demo_binary_semaphore,
                                 FR_BINARY_SEMAPHORE_WAIT_FOREVER)) {
        ++g_fr_demo_semaphore_forever_signals;
    }

    fr_demo_update_stack_diagnostics();
    fr_demo_update_kernel_invariants();
    fr_demo_update_trace_snapshot();
    fr_demo_stress_update_diagnostics();

    if (!fr_demo_stress_start()) {
        ++g_fr_demo_stress_integration_errors;
    }

    while (1) {

        fr_demo_queue_message_t message;

        if (fr_queue_receive(&g_fr_demo_queue, &message)) {
            ++g_fr_demo_queue_receive_successes;

            const uint32_t expected_sequence = g_fr_demo_queue_last_received_sequence + 1u;

            if (message.sequence != expected_sequence) {
                ++g_fr_demo_queue_order_errors;
            }

            g_fr_demo_queue_last_received_sequence = message.sequence;

            g_fr_demo_queue_last_received_tick = fr_tick_now();

            g_fr_demo_queue_last_latency =
                fr_tick_elapsed(message.produced_tick,
                                g_fr_demo_queue_last_received_tick);
        } else {
            ++g_fr_demo_queue_receive_empty;
        }

        if (fr_binary_semaphore_take(&g_fr_demo_binary_semaphore, 30u)) {
            ++g_fr_demo_semaphore_take_successes;
        } else {
            ++g_fr_demo_semaphore_take_timeouts;
        }

        ++g_fr_demo_task_b_iterations;

        local_state = fr_demo_advance_local_state(local_state);

        const fr_tick_t now = fr_tick_now();

        if (fr_tick_elapsed(last_toggle_tick, now) >= FR_DEMO_LED_TOGGLE_TICKS) {
            last_toggle_tick += FR_DEMO_LED_TOGGLE_TICKS;
            fr_board_led_toggle();
        }

        if ((g_fr_demo_task_b_iterations & FR_DEMO_VALIDATION_MASK) == 0u) {
            fr_demo_validate_preempted_task(g_fr_demo_task_b,
                                            &g_fr_demo_task_b_info,
                                            stack_probe,
                                            g_fr_demo_task_b_stack_pattern,
                                            local_state,
                                            &g_fr_demo_task_b_preemption);
        }

        if ((g_fr_demo_task_b_iterations &
             FR_DEMO_STACK_DIAGNOSTIC_MASK) == 0u) {
            fr_demo_update_stack_diagnostics();
            fr_demo_stress_update_diagnostics();
        }

        if ((g_fr_demo_task_b_iterations &
             FR_DEMO_KERNEL_INVARIANT_MASK) == 0u) {
            fr_demo_update_kernel_invariants();
        }

        if ((g_fr_demo_task_b_iterations &
             FR_DEMO_TRACE_DIAGNOSTIC_MASK) == 0u) {
            fr_demo_update_trace_snapshot();
        }

        const fr_tick_t sleep_start = fr_tick_now();

        ++g_fr_demo_task_b_sleep_calls;

        if (fr_task_sleep(FR_DEMO_TASK_B_SLEEP_TICKS)) {
            ++g_fr_demo_task_b_wakeups;

            g_fr_demo_task_b_last_sleep_elapsed =
                fr_tick_elapsed(sleep_start, fr_tick_now());

            if (g_fr_demo_task_b_last_sleep_elapsed < FR_DEMO_TASK_B_SLEEP_TICKS) {
                ++g_fr_demo_task_b_sleep_errors;
            }
        } else {
            ++g_fr_demo_task_b_sleep_errors;
        }
    }
}

static void fr_demo_monitor_task_entry(void *argument) {
    (void)argument;

    ++g_fr_demo_monitor_task_entry_count;

    (void)fr_demo_uart_write_text(
        "\r\nForgeRTOS UART monitor ready @ 115200 8-N-1\r\n");

    fr_demo_uart_emit_help();

    while (1) {
        uint8_t command;

        while (fr_uart_try_read(&command)) {
            fr_demo_uart_process_command(command);
        }

        if (fr_uart_get_status(&g_fr_demo_uart_status)) {
            ++g_fr_demo_uart_status_checks;
        } else {
            ++g_fr_demo_uart_errors;
        }

        if (!fr_task_sleep(FR_DEMO_MONITOR_POLL_TICKS)) {
            ++g_fr_demo_uart_errors;
        }
    }
}

static void fr_demo_task_c_entry(void *argument) {
    (void)argument;

    ++g_fr_demo_task_c_entry_count;

    /*
     * Do not participate until the low-priority owner has acquired
     * the inversion-test mutex.
     */
    if (!fr_binary_semaphore_take(
            &g_fr_demo_inversion_medium_gate,
            FR_BINARY_SEMAPHORE_WAIT_FOREVER)) {
        ++g_fr_demo_inversion_errors;

        while (1) {
            (void)fr_task_sleep(FR_TASK_SLEEP_MAX_TICKS);
        }
    }

    ++g_fr_demo_inversion_medium_started;

    if ((g_fr_demo_inheritance_high_before_medium == 0u) ||
        (g_fr_demo_inversion_high_lock_successes != 1u) ||
        (g_fr_demo_inversion_high_unlock_successes != 1u)) {
        ++g_fr_demo_inversion_errors;
    }

    g_fr_demo_inversion_medium_start_tick = fr_tick_now();

    while (fr_tick_elapsed(g_fr_demo_inversion_medium_start_tick,
                           fr_tick_now()) <
           FR_DEMO_INVERSION_MEDIUM_RUN_TICKS) {
        ++g_fr_demo_inversion_medium_iterations;
    }

    g_fr_demo_inversion_medium_end_tick = fr_tick_now();

    g_fr_demo_inversion_medium_run_elapsed =
        fr_tick_elapsed(g_fr_demo_inversion_medium_start_tick,
                        g_fr_demo_inversion_medium_end_tick);

    ++g_fr_demo_inversion_medium_completed;

    if ((g_fr_demo_inheritance_high_before_medium != 0u) &&
        (g_fr_demo_inversion_high_wait_elapsed <
         g_fr_demo_inversion_medium_run_elapsed)) {
        g_fr_demo_inheritance_observed = 1u;
    } else {
        ++g_fr_demo_inversion_errors;
    }

    /*
     * For now we need this task only once. Park it for the longest
     * supported finite sleep interval.
     */
    while (1) {
        if (!fr_task_sleep(FR_TASK_SLEEP_MAX_TICKS)) {
            ++g_fr_demo_inversion_errors;
        }
    }
}

static bool fr_demo_create_tasks(void) {
    const fr_task_config_t task_a_config = {
        .entry = fr_demo_task_a_entry,
        .argument = &g_fr_demo_task_a_argument,
        .stack_memory = g_fr_demo_task_a_stack,
        .stack_size_words = FR_DEMO_TASK_A_STACK_WORDS,
        .priority = FR_DEMO_TASK_A_PRIORITY
    };

    const fr_task_config_t task_b_config = {
        .entry = fr_demo_task_b_entry,
        .argument = &g_fr_demo_task_b_argument,
        .stack_memory = g_fr_demo_task_b_stack,
        .stack_size_words = FR_DEMO_TASK_B_STACK_WORDS,
        .priority = FR_DEMO_TASK_B_PRIORITY
    };

    const fr_task_config_t task_c_config = {
        .entry = fr_demo_task_c_entry,
        .argument = NULL,
        .stack_memory = g_fr_demo_task_c_stack,
        .stack_size_words = FR_DEMO_TASK_C_STACK_WORDS,
        .priority = FR_DEMO_TASK_C_PRIORITY
    };

    const fr_task_config_t monitor_task_config = {
        .entry = fr_demo_monitor_task_entry,
        .argument = NULL,
        .stack_memory = g_fr_demo_monitor_task_stack,
        .stack_size_words = FR_DEMO_MONITOR_TASK_STACK_WORDS,
        .priority = FR_DEMO_MONITOR_TASK_PRIORITY
    };

    g_fr_demo_task_a_status = fr_task_create(&g_fr_demo_task_a, &task_a_config);
    g_fr_demo_task_b_status = fr_task_create(&g_fr_demo_task_b, &task_b_config);
    g_fr_demo_task_c_status = fr_task_create(&g_fr_demo_task_c, &task_c_config);
    g_fr_demo_monitor_task_status =
        fr_task_create(&g_fr_demo_monitor_task, &monitor_task_config);

    if ((g_fr_demo_task_a_status != FR_TASK_OK) ||
        (g_fr_demo_task_b_status != FR_TASK_OK) ||
        (g_fr_demo_task_c_status != FR_TASK_OK) ||
        (g_fr_demo_monitor_task_status != FR_TASK_OK)) {
        return false;
    }

    if (!fr_task_get_info(g_fr_demo_task_a, &g_fr_demo_task_a_info)) {
        return false;
    }

    if (!fr_task_get_info(g_fr_demo_task_b, &g_fr_demo_task_b_info)) {
        return false;
    }

    if (!fr_task_get_info(g_fr_demo_monitor_task,
                          &g_fr_demo_monitor_task_info)) {
        return false;
    }

    if (!fr_demo_stress_create_tasks()) {
        ++g_fr_demo_stress_integration_errors;
        return false;
    }

    g_fr_demo_task_count_snapshot = fr_task_count();

    return true;
}

static _Noreturn void fr_bootstrap_entry(void) {
    fr_assert_init();

    if ((g_fr_assert_record.magic != 0u) ||
        (g_fr_assert_record.count != 0u)) {
        ++g_fr_demo_assert_init_failures;
    }

    fr_fault_init();
    fr_board_init();

    if (!fr_uart_init(FR_BOARD_RESET_CORE_CLOCK_HZ,
                      FR_DEMO_UART_BAUD_RATE)) {
        ++g_fr_demo_uart_init_failures;
    }

    if (!fr_uart_get_status(&g_fr_demo_uart_status) ||
        !g_fr_demo_uart_status.initialized ||
        (g_fr_demo_uart_status.peripheral_clock_hz !=
         FR_BOARD_RESET_CORE_CLOCK_HZ) ||
        (g_fr_demo_uart_status.baud_rate !=
         FR_DEMO_UART_BAUD_RATE) ||
        (g_fr_demo_uart_status.baud_divisor == 0u)) {
        ++g_fr_demo_uart_init_failures;
    }

    if (g_fr_demo_uart_init_failures != 0u) {
        while (1) {
        }
    }

    g_fr_demo_timeout_math_failures = fr_demo_test_timeout_math();

    if (!fr_systick_init(FR_BOARD_RESET_CORE_CLOCK_HZ, FR_DEMO_TICK_HZ)) {
        while (1) {
        }
    }

    fr_trace_init();

    if (!fr_trace_get_status(&g_fr_demo_trace_status) ||
        !g_fr_demo_trace_status.enabled ||
        (g_fr_demo_trace_status.capacity != FR_TRACE_BUFFER_CAPACITY) ||
        (g_fr_demo_trace_status.count != 0u) ||
        (g_fr_demo_trace_status.write_index != 0u) ||
        (g_fr_demo_trace_status.next_sequence != 1u) ||
        (g_fr_demo_trace_status.overwritten_events != 0u)) {
        ++g_fr_demo_trace_init_failures;
    }

    if (g_fr_demo_trace_init_failures != 0u) {
        while (1) {
        }
    }

    if (!fr_binary_semaphore_init(&g_fr_demo_binary_semaphore, false)) {
        ++g_fr_demo_semaphore_init_failures;
    }

    if (!fr_binary_semaphore_init(&g_fr_demo_semaphore_probe, true)) {
        ++g_fr_demo_semaphore_init_failures;
    }

    if (g_fr_demo_semaphore_init_failures != 0u) {
        while (1) {
        }
    }

    if (fr_counting_semaphore_init(&g_fr_demo_counting_invalid_probe,
                                    0u,
                                    0u)) {
        ++g_fr_demo_counting_validation_failures;
    }

    if (fr_counting_semaphore_init(&g_fr_demo_counting_invalid_probe,
                                    4u,
                                    3u)) {
        ++g_fr_demo_counting_validation_failures;
    }

    if (!fr_counting_semaphore_init(&g_fr_demo_counting_semaphore,
                                    0u,
                                    3u)) {
        ++g_fr_demo_counting_init_failures;
    }

    if (!fr_counting_semaphore_init(&g_fr_demo_counting_probe,
                                    2u,
                                    3u)) {
        ++g_fr_demo_counting_init_failures;
    }

    if ((g_fr_demo_counting_init_failures != 0u) ||
        (g_fr_demo_counting_validation_failures != 0u)) {
        while (1) {
        }
    }

    if (!fr_mutex_init(&g_fr_demo_mutex)) {
        ++g_fr_demo_mutex_init_failures;
    }

    if (!fr_mutex_init(&g_fr_demo_mutex_timeout_probe)) {
        ++g_fr_demo_mutex_init_failures;
    }

    if (g_fr_demo_mutex_init_failures != 0u) {
        while (1) {
        }
    }

    if (!fr_mutex_init(&g_fr_demo_inversion_mutex)) {
        ++g_fr_demo_inversion_init_failures;
    }

    if (!fr_binary_semaphore_init(&g_fr_demo_inversion_high_gate, false)) {
        ++g_fr_demo_inversion_init_failures;
    }

    if (!fr_binary_semaphore_init(&g_fr_demo_inversion_medium_gate, false)) {
        ++g_fr_demo_inversion_init_failures;
    }

    if (!fr_binary_semaphore_init(&g_fr_demo_mutex_start_gate, false)) {
        ++g_fr_demo_inversion_init_failures;
    }

    if (g_fr_demo_inversion_init_failures != 0u) {
        while (1) {
        }
    }

    if (fr_queue_init(&g_fr_demo_queue_invalid_probe,
                    g_fr_demo_queue_invalid_storage,
                    0u,
                    sizeof(uint32_t))) {
        ++g_fr_demo_queue_validation_failures;
    }

    if (fr_queue_init(&g_fr_demo_queue_invalid_probe,
                    g_fr_demo_queue_invalid_storage,
                    1u,
                    0u)) {
        ++g_fr_demo_queue_validation_failures;
    }

    if (fr_queue_init(&g_fr_demo_queue_invalid_probe,
                    g_fr_demo_queue_invalid_storage,
                    UINT32_MAX,
                    2u)) {
        ++g_fr_demo_queue_validation_failures;
    }

    if (!fr_queue_init(&g_fr_demo_queue,
                        g_fr_demo_queue_storage,
                        FR_DEMO_QUEUE_CAPACITY,
                        (uint32_t)sizeof(fr_demo_queue_message_t))) {
            ++g_fr_demo_queue_init_failures;
    }

    if (!fr_queue_init(&g_fr_demo_queue_probe,
                        g_fr_demo_queue_probe_storage,
                        FR_DEMO_QUEUE_PROBE_CAPACITY,
                        (uint32_t)sizeof(uint32_t))) {
            ++g_fr_demo_queue_init_failures;
    }

    if ((g_fr_demo_queue_init_failures != 0u) ||
        (g_fr_demo_queue_validation_failures != 0u)) {
        while (1) {
        }
    }

    if (!fr_queue_init(
            &g_fr_demo_blocking_queue,
            g_fr_demo_blocking_queue_storage,
            FR_DEMO_BLOCKING_QUEUE_CAPACITY,
            (uint32_t)sizeof(uint32_t))) {
        ++g_fr_demo_blocking_queue_init_failures;
    }

    if (g_fr_demo_blocking_queue_init_failures != 0u) {
        while (1) {
        }
    }

    if (!fr_event_flags_init(&g_fr_demo_event_flags)) {
        ++g_fr_demo_event_flags_init_failures;
    }

    if (g_fr_demo_event_flags_init_failures != 0u) {
        while (1) {
        }
    }

    if (!fr_binary_semaphore_init(
            &g_fr_demo_ipc_timeout_binary,
            false)) {
        ++g_fr_demo_ipc_timeout_init_failures;
    }

    if (!fr_counting_semaphore_init(
            &g_fr_demo_ipc_timeout_counting,
            0u,
            1u)) {
        ++g_fr_demo_ipc_timeout_init_failures;
    }

    if (g_fr_demo_ipc_timeout_init_failures != 0u) {
        while (1) {
        }
    }

    if (!fr_demo_stress_init()) {
        ++g_fr_demo_stress_integration_errors;

        while (1) {
        }
    }

    if ((FR_DEMO_IDLE_ONLY == 0u) && !fr_demo_create_tasks()) {
        while (1) {
        }
    }

    g_fr_demo_scheduler_return_status = fr_scheduler_start();

    while (1) {
    }
}

int main(void) {
    fr_arch_capture_boot_snapshot();

    fr_demo_prepare_bootstrap_stack();

    uint32_t *const stack_top =
        &g_fr_demo_bootstrap_stack[FR_DEMO_BOOTSTRAP_STACK_WORDS];

    fr_arch_enter_thread_psp(stack_top, fr_bootstrap_entry);
}
