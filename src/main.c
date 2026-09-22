#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/arch/cortex_m4.h"
#include "forge/arch/fault.h"
#include "forge/arch/systick.h"
#include "forge/board.h"
#include "forge/scheduler.h"
#include "forge/semaphore.h"
#include "forge/task.h"
#include "forge/tick.h"

#define FR_DEMO_BOOTSTRAP_STACK_WORDS     128u
#define FR_DEMO_STACK_PATTERN             0xA5A5A5A5u
#define FR_DEMO_TICK_HZ                   1000u
#define FR_DEMO_LED_TOGGLE_TICKS          500u

#define FR_DEMO_TASK_A_STACK_WORDS        128u
#define FR_DEMO_TASK_B_STACK_WORDS        96u

#define FR_DEMO_VALIDATION_MASK           0x0000000Fu
#define FR_DEMO_SCHEDULER_NOT_RETURNED    0xFFu

#define FR_DEMO_TURN_TASK_A                1u
#define FR_DEMO_TURN_TASK_B                2u

#define FR_DEMO_TASK_A_LOCAL_STATE_SEED    0x13579BDFu
#define FR_DEMO_TASK_B_LOCAL_STATE_SEED    0x2468ACE0u

#ifndef FR_DEMO_TASK_A_PRIORITY
#define FR_DEMO_TASK_A_PRIORITY 7u
#endif

#ifndef FR_DEMO_TASK_B_PRIORITY
#define FR_DEMO_TASK_B_PRIORITY 7u
#endif

#ifndef FR_DEMO_IDLE_ONLY
#define FR_DEMO_IDLE_ONLY 0u
#endif

#define FR_DEMO_TASK_A_SLEEP_TICKS 5u
#define FR_DEMO_TASK_B_SLEEP_TICKS 13u

_Alignas(8) uint32_t g_fr_demo_bootstrap_stack[FR_DEMO_BOOTSTRAP_STACK_WORDS];
_Alignas(8) uint32_t g_fr_demo_task_a_stack[FR_DEMO_TASK_A_STACK_WORDS];
_Alignas(8) uint32_t g_fr_demo_task_b_stack[FR_DEMO_TASK_B_STACK_WORDS];

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

fr_task_handle_t g_fr_demo_task_a;
fr_task_handle_t g_fr_demo_task_b;

fr_task_info_t g_fr_demo_task_a_info;
fr_task_info_t g_fr_demo_task_b_info;

fr_task_status_t g_fr_demo_task_a_status;
fr_task_status_t g_fr_demo_task_b_status;

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

_Static_assert((FR_DEMO_BOOTSTRAP_STACK_WORDS % 2u) == 0u, "Bootstrap stack size must preserve 8-byte alignment");
_Static_assert((FR_DEMO_TASK_A_STACK_WORDS % 2u) == 0u, "Task A stack size must preserve 8-byte alignment");
_Static_assert((FR_DEMO_TASK_B_STACK_WORDS % 2u) == 0u, "Task B stack size must preserve 8-byte alignment");
_Static_assert((FR_BOARD_RESET_CORE_CLOCK_HZ % FR_DEMO_TICK_HZ) == 0u, "SysTick frequency must divide core clock exactly");

static void fr_demo_prepare_bootstrap_stack(void) {
    for (uint32_t i = 0u; i < FR_DEMO_BOOTSTRAP_STACK_WORDS; ++i) {
        g_fr_demo_bootstrap_stack[i] = FR_DEMO_STACK_PATTERN;
    }
}


static uint32_t fr_demo_advance_local_state(uint32_t state) {
    return (state * 1664525u) + 1013904223u;
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

    while (1) {
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
        ++g_fr_demo_task_a_sleep_calls;

        if (fr_task_sleep(FR_DEMO_TASK_A_SLEEP_TICKS)) {
            ++g_fr_demo_task_a_wakeups;
        }else {
            ++g_fr_demo_task_a_sleep_errors;
        }

        const fr_tick_t sleep_start = fr_tick_now();

        ++g_fr_demo_task_a_sleep_calls;

        if (fr_task_sleep(FR_DEMO_TASK_A_SLEEP_TICKS)) {
            ++g_fr_demo_task_a_wakeups;

            g_fr_demo_task_a_last_sleep_elapsed =
                fr_tick_elapsed(sleep_start, fr_tick_now());

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

    while (1) {

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
        ++g_fr_demo_task_b_sleep_calls;

        if (fr_task_sleep(FR_DEMO_TASK_B_SLEEP_TICKS)) {
            ++g_fr_demo_task_b_wakeups;
        } else {
            ++g_fr_demo_task_b_sleep_errors;
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

    g_fr_demo_task_a_status = fr_task_create(&g_fr_demo_task_a, &task_a_config);
    g_fr_demo_task_b_status = fr_task_create(&g_fr_demo_task_b, &task_b_config);

    if ((g_fr_demo_task_a_status != FR_TASK_OK) ||
        (g_fr_demo_task_b_status != FR_TASK_OK)) {
        return false;
    }

    if (!fr_task_get_info(g_fr_demo_task_a, &g_fr_demo_task_a_info)) {
        return false;
    }

    if (!fr_task_get_info(g_fr_demo_task_b, &g_fr_demo_task_b_info)) {
        return false;
    }

    g_fr_demo_task_count_snapshot = fr_task_count();

    return true;
}

static _Noreturn void fr_bootstrap_entry(void) {
    fr_fault_init();
    fr_board_init();

    g_fr_demo_timeout_math_failures = fr_demo_test_timeout_math();

    if (!fr_systick_init(FR_BOARD_RESET_CORE_CLOCK_HZ, FR_DEMO_TICK_HZ)) {
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
