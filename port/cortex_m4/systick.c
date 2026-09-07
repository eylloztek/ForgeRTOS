#include "forge/arch/systick.h"
#include "forge/kernel/tick_internal.h"

#include "port_internal.h"

#define FR_SYSTICK_BASE                    0xE000E010u

#define FR_SYSTICK_CTRL_ENABLE             (1u << 0)
#define FR_SYSTICK_CTRL_TICKINT            (1u << 1)
#define FR_SYSTICK_CTRL_CLKSOURCE          (1u << 2)

#define FR_SYSTICK_RELOAD_MAX              0x00FFFFFFu
#define FR_SYSTICK_MAX_COUNTER_CYCLES      (FR_SYSTICK_RELOAD_MAX + 1u)

#define FR_SCB_SHPR3                        (*(volatile uint32_t *)0xE000ED20u)
#define FR_SCB_SHPR3_SYSTICK_SHIFT         24u
#define FR_SCB_SHPR3_SYSTICK_MASK          (0xFFu << FR_SCB_SHPR3_SYSTICK_SHIFT)

typedef struct {
    volatile uint32_t ctrl;
    volatile uint32_t load;
    volatile uint32_t val;
    volatile const uint32_t calib;
} fr_systick_registers_t;

#define FR_SYSTICK ((fr_systick_registers_t *)FR_SYSTICK_BASE)

static void fr_systick_set_priority(uint32_t logical_priority) {
    const uint32_t encoded_priority = FR_CORTEX_M4_ENCODE_PRIORITY(logical_priority);

    uint32_t shpr3 = FR_SCB_SHPR3;
    shpr3 &= ~FR_SCB_SHPR3_SYSTICK_MASK;
    shpr3 |= encoded_priority << FR_SCB_SHPR3_SYSTICK_SHIFT;
    FR_SCB_SHPR3 = shpr3;
}

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

    fr_systick_set_priority(FR_CONFIG_KERNEL_INTERRUPT_CEILING);

    FR_SYSTICK->load = cycles_per_tick - 1u;
    FR_SYSTICK->val = 0u;

    FR_SYSTICK->ctrl = FR_SYSTICK_CTRL_CLKSOURCE |
                       FR_SYSTICK_CTRL_TICKINT |
                       FR_SYSTICK_CTRL_ENABLE;

    return true;
}

void SysTick_Handler(void) {
    fr_kernel_tick_isr();
}