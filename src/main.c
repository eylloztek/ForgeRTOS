#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/arch/context_probe.h"
#include "forge/arch/cortex_m4.h"
#include "forge/arch/fault.h"
#include "forge/arch/systick.h"
#include "forge/board.h"
#include "forge/scheduler.h"
#include "forge/task.h"
#include "forge/tick.h"

#define FR_DEMO_BOOTSTRAP_STACK_WORDS     128u
#define FR_DEMO_STACK_PATTERN             0xA5A5A5A5u
#define FR_DEMO_TICK_HZ                   1000u
#define FR_DEMO_LED_TOGGLE_TICKS          500u

#define FR_DEMO_TASK_A_STACK_WORDS        128u
#define FR_DEMO_TASK_B_STACK_WORDS        96u

#define FR_DEMO_YIELD_MASK                0x00003FFFu
#define FR_DEMO_SCHEDULER_NOT_RETURNED    0xFFu

_Alignas(8) uint32_t g_fr_demo_bootstrap_stack[FR_DEMO_BOOTSTRAP_STACK_WORDS];
_Alignas(8) uint32_t g_fr_demo_task_a_stack[FR_DEMO_TASK_A_STACK_WORDS];
_Alignas(8) uint32_t g_fr_demo_task_b_stack[FR_DEMO_TASK_B_STACK_WORDS];

uint32_t g_fr_demo_task_a_argument = 0xA1A1A1A1u;
uint32_t g_fr_demo_task_b_argument = 0xB2B2B2B2u;

static const fr_arch_context_pattern_t g_fr_demo_task_a_context_pattern = {
    .r4 = 0xA4A4A4A4u,
    .r5 = 0xA5A5A5A5u,
    .r6 = 0xA6A6A6A6u,
    .r7 = 0xA7A7A7A7u,
    .r8 = 0xA8A8A8A8u,
    .r9 = 0xA9A9A9A9u,
    .r10 = 0xAAAAAAAAu,
    .r11 = 0xABABABABu
};

static const fr_arch_context_pattern_t g_fr_demo_task_b_context_pattern = {
    .r4 = 0xB4B4B4B4u,
    .r5 = 0xB5B5B5B5u,
    .r6 = 0xB6B6B6B6u,
    .r7 = 0xB7B7B7B7u,
    .r8 = 0xB8B8B8B8u,
    .r9 = 0xB9B9B9B9u,
    .r10 = 0xBABABABAu,
    .r11 = 0xBBBBBBBBu
};

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

volatile uint32_t g_fr_demo_task_a_yields;
volatile uint32_t g_fr_demo_task_b_yields;

volatile uint32_t g_fr_demo_task_a_resumes;
volatile uint32_t g_fr_demo_task_b_resumes;

volatile uint32_t g_fr_demo_task_a_argument_value;
volatile uint32_t g_fr_demo_task_b_argument_value;

volatile uint32_t g_fr_demo_task_a_context_checks;
volatile uint32_t g_fr_demo_task_b_context_checks;

volatile uint32_t g_fr_demo_task_a_context_failures;
volatile uint32_t g_fr_demo_task_b_context_failures;

uint32_t g_fr_demo_task_count_snapshot;

volatile fr_scheduler_status_t g_fr_demo_scheduler_return_status =
    FR_DEMO_SCHEDULER_NOT_RETURNED;

_Static_assert((FR_DEMO_BOOTSTRAP_STACK_WORDS % 2u) == 0u, "Bootstrap stack size must preserve 8-byte alignment");
_Static_assert((FR_DEMO_TASK_A_STACK_WORDS % 2u) == 0u, "Task A stack size must preserve 8-byte alignment");
_Static_assert((FR_DEMO_TASK_B_STACK_WORDS % 2u) == 0u, "Task B stack size must preserve 8-byte alignment");
_Static_assert((FR_BOARD_RESET_CORE_CLOCK_HZ % FR_DEMO_TICK_HZ) == 0u, "SysTick frequency must divide core clock exactly");

static void fr_demo_prepare_bootstrap_stack(void) {
    for (uint32_t i = 0u; i < FR_DEMO_BOOTSTRAP_STACK_WORDS; ++i) {
        g_fr_demo_bootstrap_stack[i] = FR_DEMO_STACK_PATTERN;
    }
}

static void fr_demo_task_a_entry(void *argument) {
    g_fr_demo_task_a_started = 1u;

    if (argument != NULL) {
        g_fr_demo_task_a_argument_value = *(const uint32_t *)argument;
    }

    while (1) {
        ++g_fr_demo_task_a_iterations;

        if ((g_fr_demo_task_a_iterations & FR_DEMO_YIELD_MASK) == 0u) {
            ++g_fr_demo_task_a_yields;

            const bool context_valid =
                fr_arch_context_probe_yield(&g_fr_demo_task_a_context_pattern);

            ++g_fr_demo_task_a_resumes;
            ++g_fr_demo_task_a_context_checks;

            if (!context_valid) {
                ++g_fr_demo_task_a_context_failures;
            }
        }
    }
}

static void fr_demo_task_b_entry(void *argument) {
    g_fr_demo_task_b_started = 1u;

    if (argument != NULL) {
        g_fr_demo_task_b_argument_value = *(const uint32_t *)argument;
    }

    fr_tick_t last_toggle_tick = fr_tick_now();

    while (1) {
        ++g_fr_demo_task_b_iterations;

        const fr_tick_t now = fr_tick_now();

        if (fr_tick_elapsed(last_toggle_tick, now) >= FR_DEMO_LED_TOGGLE_TICKS) {
            last_toggle_tick += FR_DEMO_LED_TOGGLE_TICKS;
            fr_board_led_toggle();
        }

        if ((g_fr_demo_task_b_iterations & FR_DEMO_YIELD_MASK) == 0u) {
            ++g_fr_demo_task_b_yields;

            const bool context_valid =
                fr_arch_context_probe_yield(&g_fr_demo_task_b_context_pattern);

            ++g_fr_demo_task_b_resumes;
            ++g_fr_demo_task_b_context_checks;

            if (!context_valid) {
                ++g_fr_demo_task_b_context_failures;
            }
        }
    }
}

static bool fr_demo_create_tasks(void) {
    const fr_task_config_t task_a_config = {
        .entry = fr_demo_task_a_entry,
        .argument = &g_fr_demo_task_a_argument,
        .stack_memory = g_fr_demo_task_a_stack,
        .stack_size_words = FR_DEMO_TASK_A_STACK_WORDS,
        .priority = 3u
    };

    const fr_task_config_t task_b_config = {
        .entry = fr_demo_task_b_entry,
        .argument = &g_fr_demo_task_b_argument,
        .stack_memory = g_fr_demo_task_b_stack,
        .stack_size_words = FR_DEMO_TASK_B_STACK_WORDS,
        .priority = 7u
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

    if (!fr_systick_init(FR_BOARD_RESET_CORE_CLOCK_HZ, FR_DEMO_TICK_HZ)) {
        while (1) {
        }
    }

    if (!fr_demo_create_tasks()) {
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