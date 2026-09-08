#include <stdint.h>

#include "forge/arch/cortex_m4.h"
#include "forge/arch/fault.h"
#include "forge/arch/systick.h"
#include "forge/board.h"
#include "forge/tick.h"

#define FR_DEMO_PSP_STACK_WORDS      128u
#define FR_DEMO_STACK_PATTERN        0xA5A5A5A5u
#define FR_DEMO_TICK_HZ              1000u
#define FR_DEMO_LED_TOGGLE_TICKS     500u

_Alignas(8) uint32_t g_fr_demo_psp_stack[FR_DEMO_PSP_STACK_WORDS];

volatile uint32_t g_fr_demo_fault_test_enabled;

_Static_assert((FR_DEMO_PSP_STACK_WORDS % 2u) == 0u, "PSP stack size must preserve 8-byte alignment");
_Static_assert((FR_BOARD_RESET_CORE_CLOCK_HZ % FR_DEMO_TICK_HZ) == 0u, "SysTick frequency must divide core clock exactly");

static void fr_demo_prepare_psp_stack(void) {
    for (uint32_t i = 0u; i < FR_DEMO_PSP_STACK_WORDS; ++i) {
        g_fr_demo_psp_stack[i] = FR_DEMO_STACK_PATTERN;
    }
}

static void fr_demo_fault_probe(void) {
    if (g_fr_demo_fault_test_enabled != 0u) {
        __asm volatile("udf #0");
    }
}

static _Noreturn void fr_psp_demo_entry(void) {
    fr_fault_init();
    fr_board_init();

    if (!fr_systick_init(FR_BOARD_RESET_CORE_CLOCK_HZ, FR_DEMO_TICK_HZ)) {
        while (1) {
        }
    }

    fr_demo_fault_probe();

    fr_tick_t last_toggle_tick = fr_tick_now();

    while (1) {
        const fr_tick_t now = fr_tick_now();

        if (fr_tick_elapsed(last_toggle_tick, now) >= FR_DEMO_LED_TOGGLE_TICKS) {
            last_toggle_tick += FR_DEMO_LED_TOGGLE_TICKS;
            fr_board_led_toggle();
        }
    }
}

int main(void) {
    fr_arch_capture_boot_snapshot();

    fr_demo_prepare_psp_stack();

    uint32_t *const psp_top = &g_fr_demo_psp_stack[FR_DEMO_PSP_STACK_WORDS];

    fr_arch_enter_thread_psp(psp_top, fr_psp_demo_entry);
}