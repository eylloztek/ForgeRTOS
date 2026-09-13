#ifndef FORGE_CORTEX_M4_PORT_INTERNAL_H
#define FORGE_CORTEX_M4_PORT_INTERNAL_H

#include <stdint.h>

#include "forge/config.h"

#define FR_CORTEX_M4_NVIC_PRIORITY_BITS        4u
#define FR_CORTEX_M4_PRIORITY_LEVEL_COUNT      (1u << FR_CORTEX_M4_NVIC_PRIORITY_BITS)
#define FR_CORTEX_M4_PRIORITY_SHIFT            (8u - FR_CORTEX_M4_NVIC_PRIORITY_BITS)

#define FR_CORTEX_M4_ENCODE_PRIORITY(priority) \
    ((uint32_t)(priority) << FR_CORTEX_M4_PRIORITY_SHIFT)

#define FR_CORTEX_M4_PENDSV_LOGICAL_PRIORITY \
    (FR_CORTEX_M4_PRIORITY_LEVEL_COUNT - 1u)

_Static_assert(FR_CONFIG_KERNEL_INTERRUPT_CEILING > 0u,
               "Kernel interrupt ceiling must not be zero");

_Static_assert(FR_CONFIG_KERNEL_INTERRUPT_CEILING < FR_CORTEX_M4_PRIORITY_LEVEL_COUNT,
               "Kernel interrupt ceiling exceeds Cortex-M4 priority range");

_Static_assert(FR_CORTEX_M4_PENDSV_LOGICAL_PRIORITY >= FR_CONFIG_KERNEL_INTERRUPT_CEILING,
               "PendSV must be masked by the kernel interrupt ceiling");

#endif