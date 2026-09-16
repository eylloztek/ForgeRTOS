#include <stdint.h>

#include "forge/kernel/port.h"

#include "port_internal.h"

#define FR_SCB_ICSR                         (*(volatile uint32_t *)0xE000ED04u)
#define FR_SCB_SHPR3                        (*(volatile uint32_t *)0xE000ED20u)

#define FR_SCB_ICSR_PENDSVCLR               (1u << 27)
#define FR_SCB_ICSR_PENDSVSET               (1u << 28)

#define FR_SCB_SHPR3_PENDSV_SHIFT           16u
#define FR_SCB_SHPR3_PENDSV_MASK            (0xFFu << FR_SCB_SHPR3_PENDSV_SHIFT)

void fr_port_scheduler_init(void) {
    const uint32_t encoded_priority =
        FR_CORTEX_M4_ENCODE_PRIORITY(FR_CORTEX_M4_PENDSV_LOGICAL_PRIORITY);

    uint32_t shpr3 = FR_SCB_SHPR3;

    shpr3 &= ~FR_SCB_SHPR3_PENDSV_MASK;
    shpr3 |= encoded_priority << FR_SCB_SHPR3_PENDSV_SHIFT;

    FR_SCB_SHPR3 = shpr3;

    FR_SCB_ICSR = FR_SCB_ICSR_PENDSVCLR;

    __asm volatile(
        "dsb\n"
        "isb\n"
        :
        :
        : "memory"
    );
}

void fr_port_request_context_switch(void) {
    FR_SCB_ICSR = FR_SCB_ICSR_PENDSVSET;

    __asm volatile(
        "dsb\n"
        "isb\n"
        :
        :
        : "memory"
    );
}