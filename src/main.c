#include <stdint.h>

#include "demo.h"
#include "monitor.h"
#include "forge/arch/cortex_m4.h"
#include "forge/arch/fault.h"
#include "forge/arch/systick.h"
#include "forge/assert.h"
#include "forge/board.h"
#include "forge/scheduler.h"
#include "forge/trace.h"
#include "forge/uart.h"

#define FR_RELEASE_BOOTSTRAP_STACK_WORDS 128u
#define FR_RELEASE_STACK_PATTERN         0xA5A5A5A5u
#define FR_RELEASE_TICK_HZ               1000u
#define FR_RELEASE_UART_BAUD_RATE        115200u

_Alignas(8) static uint32_t
    g_fr_release_bootstrap_stack[FR_RELEASE_BOOTSTRAP_STACK_WORDS];

_Static_assert((FR_RELEASE_BOOTSTRAP_STACK_WORDS % 2u) == 0u,
               "Bootstrap stack must preserve 8-byte alignment");
_Static_assert((FR_BOARD_RESET_CORE_CLOCK_HZ % FR_RELEASE_TICK_HZ) == 0u,
               "SysTick frequency must divide the core clock exactly");

static void fr_release_prepare_bootstrap_stack(void) {
    for (uint32_t i = 0u; i < FR_RELEASE_BOOTSTRAP_STACK_WORDS; ++i) {
        g_fr_release_bootstrap_stack[i] = FR_RELEASE_STACK_PATTERN;
    }
}

static _Noreturn void fr_release_halt(void) {
    while (1) {
    }
}

static _Noreturn void fr_release_bootstrap_entry(void) {
    fr_assert_init();
    fr_fault_init();
    fr_board_init();

    if (!fr_uart_init(FR_BOARD_RESET_CORE_CLOCK_HZ,
                      FR_RELEASE_UART_BAUD_RATE)) {
        fr_release_halt();
    }

    if (!fr_systick_init(FR_BOARD_RESET_CORE_CLOCK_HZ,
                         FR_RELEASE_TICK_HZ)) {
        fr_release_halt();
    }

    fr_trace_init();

    if (!fr_release_demo_init() ||
        !fr_release_demo_create_tasks() ||
        !fr_release_monitor_create_task()) {
        fr_release_halt();
    }

    (void)fr_scheduler_start();
    fr_release_halt();
}

int main(void) {
    fr_arch_capture_boot_snapshot();
    fr_release_prepare_bootstrap_stack();

    uint32_t *const stack_top =
        &g_fr_release_bootstrap_stack[FR_RELEASE_BOOTSTRAP_STACK_WORDS];

    fr_arch_enter_thread_psp(stack_top, fr_release_bootstrap_entry);
}
