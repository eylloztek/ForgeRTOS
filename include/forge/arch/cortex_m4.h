#ifndef FORGE_ARCH_CORTEX_M4_H
#define FORGE_ARCH_CORTEX_M4_H

#include <stdint.h>

typedef void (*fr_arch_entry_fn_t)(void);

typedef struct {
    uint32_t vtor;
    uint32_t initial_msp;
    uint32_t reset_vector;
    uint32_t current_msp;
    uint32_t psp;
    uint32_t control;
    uint32_t ipsr;
} fr_arch_boot_snapshot_t;

typedef struct {
    uint32_t sp;
    uint32_t msp;
    uint32_t psp;
    uint32_t control;
    uint32_t ipsr;
} fr_arch_stack_state_t;

extern volatile fr_arch_boot_snapshot_t g_fr_arch_boot_snapshot;

void fr_arch_capture_boot_snapshot(void);
void fr_arch_capture_stack_state(volatile fr_arch_stack_state_t *state);

_Noreturn void fr_arch_enter_thread_psp(uint32_t *stack_top, fr_arch_entry_fn_t entry);

#endif