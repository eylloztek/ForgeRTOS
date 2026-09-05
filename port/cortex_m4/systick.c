#include "forge/arch/systick.h"

#define FR_SYSTICK_BASE                    0xE000E010u
#define FR_SYSTICK_CTRL_ENABLE             (1u << 0)
#define FR_SYSTICK_CTRL_TICKINT            (1u << 1)
#define FR_SYSTICK_CTRL_CLKSOURCE          (1u << 2)
#define FR_SYSTICK_RELOAD_MAX              0x00FFFFFFu
#define FR_SYSTICK_MAX_COUNTER_CYCLES      (FR_SYSTICK_RELOAD_MAX + 1u)

typedef struct {
    volatile uint32_t ctrl;
    volatile uint32_t load;
    volatile uint32_t val;
    volatile const uint32_t calib;
} fr_systick_registers_t;

#define FR_SYSTICK ((fr_systick_registers_t *)FR_SYSTICK_BASE)

static volatile uint32_t g_fr_systick_ticks;

bool fr_systick_init(uint32_t core_clock_hz, uint32_t tick_hz) {
    if ((core_clock_hz == 0u) || (tick_hz == 0u)) {
        return false;
    }

    if ((core_clock_hz % tick_hz) != 0u) {
        return false;
    }

    const uint32_t cycles_per_tick = core_clock_hz / tick_hz;

    if ((cycles_per_tick == 0u) || (cycles_per_tick > FR_SYSTICK_MAX_COUNTER_CYCLES)) {
        return false;
    }

    FR_SYSTICK->ctrl = 0u;
    FR_SYSTICK->load = cycles_per_tick - 1u;
    FR_SYSTICK->val = 0u;

    g_fr_systick_ticks = 0u;

    FR_SYSTICK->ctrl = FR_SYSTICK_CTRL_CLKSOURCE |
                       FR_SYSTICK_CTRL_TICKINT |
                       FR_SYSTICK_CTRL_ENABLE;

    return true;
}

uint32_t fr_systick_get_ticks(void) {
    return g_fr_systick_ticks;
}

void SysTick_Handler(void) {
    ++g_fr_systick_ticks;
}