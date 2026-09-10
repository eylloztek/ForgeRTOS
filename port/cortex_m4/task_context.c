#include <stddef.h>
#include <stdint.h>

#include "forge/config.h"
#include "forge/kernel/port.h"

#define FR_CORTEX_M4_INITIAL_XPSR              0x01000000u

#define FR_CORTEX_M4_INITIAL_R1                0x11111111u
#define FR_CORTEX_M4_INITIAL_R2                0x22222222u
#define FR_CORTEX_M4_INITIAL_R3                0x33333333u
#define FR_CORTEX_M4_INITIAL_R4                0x44444444u
#define FR_CORTEX_M4_INITIAL_R5                0x55555555u
#define FR_CORTEX_M4_INITIAL_R6                0x66666666u
#define FR_CORTEX_M4_INITIAL_R7                0x77777777u
#define FR_CORTEX_M4_INITIAL_R8                0x88888888u
#define FR_CORTEX_M4_INITIAL_R9                0x99999999u
#define FR_CORTEX_M4_INITIAL_R10               0xAAAAAAAAu
#define FR_CORTEX_M4_INITIAL_R11               0xBBBBBBBBu
#define FR_CORTEX_M4_INITIAL_R12               0x12121212u

#define FR_CORTEX_M4_INITIAL_CONTEXT_WORDS     16u

enum {
    FR_CONTEXT_R4 = 0u,
    FR_CONTEXT_R5,
    FR_CONTEXT_R6,
    FR_CONTEXT_R7,
    FR_CONTEXT_R8,
    FR_CONTEXT_R9,
    FR_CONTEXT_R10,
    FR_CONTEXT_R11,

    FR_CONTEXT_R0,
    FR_CONTEXT_R1,
    FR_CONTEXT_R2,
    FR_CONTEXT_R3,
    FR_CONTEXT_R12,
    FR_CONTEXT_LR,
    FR_CONTEXT_PC,
    FR_CONTEXT_XPSR
};

extern _Noreturn void fr_port_task_return_trap(void);

_Static_assert(sizeof(uintptr_t) == sizeof(uint32_t), "Cortex-M4 port requires 32-bit addresses");
_Static_assert(FR_CONFIG_MIN_TASK_STACK_WORDS >= FR_CORTEX_M4_INITIAL_CONTEXT_WORDS,
               "Minimum task stack must fit the initial Cortex-M4 context");

uint32_t *fr_port_task_stack_init(uint32_t *stack_top, fr_task_entry_t entry, void *argument) {
    uint32_t *const saved_sp = stack_top - FR_CORTEX_M4_INITIAL_CONTEXT_WORDS;

    const uint32_t entry_address = ((uint32_t)(uintptr_t)entry) & ~1u;
    const uint32_t return_address = ((uint32_t)(uintptr_t)fr_port_task_return_trap) | 1u;

    saved_sp[FR_CONTEXT_R4] = FR_CORTEX_M4_INITIAL_R4;
    saved_sp[FR_CONTEXT_R5] = FR_CORTEX_M4_INITIAL_R5;
    saved_sp[FR_CONTEXT_R6] = FR_CORTEX_M4_INITIAL_R6;
    saved_sp[FR_CONTEXT_R7] = FR_CORTEX_M4_INITIAL_R7;
    saved_sp[FR_CONTEXT_R8] = FR_CORTEX_M4_INITIAL_R8;
    saved_sp[FR_CONTEXT_R9] = FR_CORTEX_M4_INITIAL_R9;
    saved_sp[FR_CONTEXT_R10] = FR_CORTEX_M4_INITIAL_R10;
    saved_sp[FR_CONTEXT_R11] = FR_CORTEX_M4_INITIAL_R11;

    saved_sp[FR_CONTEXT_R0] = (uint32_t)(uintptr_t)argument;
    saved_sp[FR_CONTEXT_R1] = FR_CORTEX_M4_INITIAL_R1;
    saved_sp[FR_CONTEXT_R2] = FR_CORTEX_M4_INITIAL_R2;
    saved_sp[FR_CONTEXT_R3] = FR_CORTEX_M4_INITIAL_R3;
    saved_sp[FR_CONTEXT_R12] = FR_CORTEX_M4_INITIAL_R12;
    saved_sp[FR_CONTEXT_LR] = return_address;
    saved_sp[FR_CONTEXT_PC] = entry_address;
    saved_sp[FR_CONTEXT_XPSR] = FR_CORTEX_M4_INITIAL_XPSR;

    return saved_sp;
}