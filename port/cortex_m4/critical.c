#include "forge/critical.h"

#include "port_internal.h"

fr_critical_state_t fr_critical_enter(void) {
    fr_critical_state_t previous_basepri;
    const uint32_t kernel_ceiling = FR_CORTEX_M4_ENCODE_PRIORITY(FR_CONFIG_KERNEL_INTERRUPT_CEILING);

    __asm volatile(
        "mrs %0, basepri\n"
        "msr basepri_max, %1\n"
        "dsb\n"
        "isb\n"
        : "=&r"(previous_basepri)
        : "r"(kernel_ceiling)
        : "memory"
    );

    return previous_basepri;
}

void fr_critical_exit(fr_critical_state_t state) {
    __asm volatile(
        "dsb\n"
        "msr basepri, %0\n"
        "isb\n"
        :
        : "r"(state)
        : "memory"
    );
}