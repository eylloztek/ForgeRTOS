#include "forge/arch/cortex_m4.h"

#define SCB_VTOR (*(volatile const uint32_t *)0xE000ED08u)

extern const uint32_t g_pfnVectors[];

volatile fr_arch_boot_snapshot_t g_fr_arch_boot_snapshot;

static uint32_t fr_arch_read_msp(void) {
    uint32_t value;
    __asm volatile("mrs %0, msp" : "=r"(value));
    return value;
}

static uint32_t fr_arch_read_psp(void) {
    uint32_t value;
    __asm volatile("mrs %0, psp" : "=r"(value));
    return value;
}

static uint32_t fr_arch_read_control(void) {
    uint32_t value;
    __asm volatile("mrs %0, control" : "=r"(value));
    return value;
}

static uint32_t fr_arch_read_ipsr(void) {
    uint32_t value;
    __asm volatile("mrs %0, ipsr" : "=r"(value));
    return value;
}

void fr_arch_capture_boot_snapshot(void) {
    g_fr_arch_boot_snapshot.vtor = SCB_VTOR;
    g_fr_arch_boot_snapshot.initial_msp = g_pfnVectors[0];
    g_fr_arch_boot_snapshot.reset_vector = g_pfnVectors[1];
    g_fr_arch_boot_snapshot.current_msp = fr_arch_read_msp();
    g_fr_arch_boot_snapshot.psp = fr_arch_read_psp();
    g_fr_arch_boot_snapshot.control = fr_arch_read_control();
    g_fr_arch_boot_snapshot.ipsr = fr_arch_read_ipsr();
}