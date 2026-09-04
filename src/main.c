#include <stdint.h>

#include "forge/arch/cortex_m4.h"
#include "forge/board.h"

#define FR_DEMO_PSP_STACK_WORDS 128u
#define FR_DEMO_STACK_PATTERN   0xA5A5A5A5u

_Alignas(8) uint32_t g_fr_demo_psp_stack[FR_DEMO_PSP_STACK_WORDS];

volatile fr_arch_stack_state_t g_fr_demo_before_switch;
volatile fr_arch_stack_state_t g_fr_demo_after_switch;
volatile fr_arch_stack_state_t g_fr_demo_after_svc;
volatile uint32_t g_fr_demo_psp_initial_top;

_Static_assert((FR_DEMO_PSP_STACK_WORDS % 2u) == 0u, "PSP stack size must preserve 8-byte alignment");

static void fr_demo_prepare_psp_stack(void) {
    for (uint32_t i = 0u; i < FR_DEMO_PSP_STACK_WORDS; ++i) {
        g_fr_demo_psp_stack[i] = FR_DEMO_STACK_PATTERN;
    }
}

static void fr_demo_trigger_svc(void) {
    __asm volatile("svc #0" ::: "memory");
}

static _Noreturn void fr_psp_demo_entry(void) {
    fr_arch_capture_stack_state(&g_fr_demo_after_switch);

    fr_demo_trigger_svc();

    fr_arch_capture_stack_state(&g_fr_demo_after_svc);

    fr_board_init();

    while (1) {
        fr_board_led_toggle();
        fr_board_delay_cycles(1000000u);
    }
}

int main(void) {
    fr_arch_capture_boot_snapshot();
    fr_arch_capture_stack_state(&g_fr_demo_before_switch);

    fr_demo_prepare_psp_stack();

    uint32_t *const psp_top = &g_fr_demo_psp_stack[FR_DEMO_PSP_STACK_WORDS];
    g_fr_demo_psp_initial_top = (uint32_t)(uintptr_t)psp_top;

    fr_arch_enter_thread_psp(psp_top, fr_psp_demo_entry);
}