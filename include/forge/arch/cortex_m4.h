#ifndef FORGE_ARCH_CORTEX_M4_H
#define FORGE_ARCH_CORTEX_M4_H

#include <stdint.h>

typedef struct {
    uint32_t vtor;
    uint32_t initial_msp;
    uint32_t reset_vector;
    uint32_t current_msp;
    uint32_t psp;
    uint32_t control;
    uint32_t ipsr;
} fr_arch_boot_snapshot_t;

extern volatile fr_arch_boot_snapshot_t g_fr_arch_boot_snapshot;

void fr_arch_capture_boot_snapshot(void);

#endif