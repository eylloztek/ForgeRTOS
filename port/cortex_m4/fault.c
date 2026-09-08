#include <stdint.h>

#include "forge/arch/fault.h"

#define FR_SCB_ICSR                     (*(volatile const uint32_t *)0xE000ED04u)
#define FR_SCB_SHCSR                    (*(volatile uint32_t *)0xE000ED24u)
#define FR_SCB_CFSR                     (*(volatile const uint32_t *)0xE000ED28u)
#define FR_SCB_HFSR                     (*(volatile const uint32_t *)0xE000ED2Cu)
#define FR_SCB_MMFAR                    (*(volatile const uint32_t *)0xE000ED34u)
#define FR_SCB_BFAR                     (*(volatile const uint32_t *)0xE000ED38u)

#define FR_SCB_SHCSR_MEMFAULTENA        (1u << 16)
#define FR_SCB_SHCSR_BUSFAULTENA        (1u << 17)
#define FR_SCB_SHCSR_USGFAULTENA        (1u << 18)

#define FR_CFSR_MSTKERR                 (1u << 4)
#define FR_CFSR_MMARVALID               (1u << 7)
#define FR_CFSR_STKERR                  (1u << 12)
#define FR_CFSR_BFARVALID               (1u << 15)

#define FR_CFSR_STACKING_ERROR_MASK     (FR_CFSR_MSTKERR | FR_CFSR_STKERR)

volatile fr_fault_record_t g_fr_fault_record;

static void fr_fault_clear_frame(void) {
    g_fr_fault_record.frame.r0 = 0u;
    g_fr_fault_record.frame.r1 = 0u;
    g_fr_fault_record.frame.r2 = 0u;
    g_fr_fault_record.frame.r3 = 0u;
    g_fr_fault_record.frame.r12 = 0u;
    g_fr_fault_record.frame.lr = 0u;
    g_fr_fault_record.frame.pc = 0u;
    g_fr_fault_record.frame.xpsr = 0u;
}

static void fr_fault_capture_frame(const fr_exception_frame_t *frame) {
    g_fr_fault_record.frame.r0 = frame->r0;
    g_fr_fault_record.frame.r1 = frame->r1;
    g_fr_fault_record.frame.r2 = frame->r2;
    g_fr_fault_record.frame.r3 = frame->r3;
    g_fr_fault_record.frame.r12 = frame->r12;
    g_fr_fault_record.frame.lr = frame->lr;
    g_fr_fault_record.frame.pc = frame->pc;
    g_fr_fault_record.frame.xpsr = frame->xpsr;
}

void fr_fault_init(void) {
    FR_SCB_SHCSR |= FR_SCB_SHCSR_MEMFAULTENA |
                    FR_SCB_SHCSR_BUSFAULTENA |
                    FR_SCB_SHCSR_USGFAULTENA;

    __asm volatile(
        "dsb\n"
        "isb\n"
        :
        :
        : "memory"
    );
}

_Noreturn void fr_fault_handle_c(const uint32_t *stack_pointer, uint32_t exception_return, uint32_t fault_kind) {
    const uint32_t cfsr = FR_SCB_CFSR;

    g_fr_fault_record.magic = 0u;

    g_fr_fault_record.kind = fault_kind;
    g_fr_fault_record.exception_return = exception_return;
    g_fr_fault_record.stack_pointer = (uint32_t)(uintptr_t)stack_pointer;

    g_fr_fault_record.cfsr = cfsr;
    g_fr_fault_record.hfsr = FR_SCB_HFSR;
    g_fr_fault_record.shcsr = FR_SCB_SHCSR;
    g_fr_fault_record.icsr = FR_SCB_ICSR;

    const uint32_t frame_valid =
        ((cfsr & FR_CFSR_STACKING_ERROR_MASK) == 0u) &&
        (stack_pointer != 0);

    g_fr_fault_record.frame_valid = frame_valid;

    if (frame_valid != 0u) {
        fr_fault_capture_frame((const fr_exception_frame_t *)stack_pointer);
    } else {
        fr_fault_clear_frame();
    }

    if ((cfsr & FR_CFSR_MMARVALID) != 0u) {
        g_fr_fault_record.mmfar = FR_SCB_MMFAR;
        g_fr_fault_record.mmfar_valid = 1u;
    } else {
        g_fr_fault_record.mmfar = 0u;
        g_fr_fault_record.mmfar_valid = 0u;
    }

    if ((cfsr & FR_CFSR_BFARVALID) != 0u) {
        g_fr_fault_record.bfar = FR_SCB_BFAR;
        g_fr_fault_record.bfar_valid = 1u;
    } else {
        g_fr_fault_record.bfar = 0u;
        g_fr_fault_record.bfar_valid = 0u;
    }

    __asm volatile("dsb" ::: "memory");

    g_fr_fault_record.magic = FR_FAULT_RECORD_MAGIC;

    while (1) {
        __asm volatile("nop");
    }
}