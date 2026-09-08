#ifndef FORGE_ARCH_FAULT_H
#define FORGE_ARCH_FAULT_H

#include <stdint.h>

#define FR_FAULT_RECORD_MAGIC 0x46524654u

typedef enum {
    FR_FAULT_HARD = 3u,
    FR_FAULT_MEMMANAGE = 4u,
    FR_FAULT_BUS = 5u,
    FR_FAULT_USAGE = 6u
} fr_fault_kind_t;

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} fr_exception_frame_t;

typedef struct {
    uint32_t magic;
    uint32_t kind;
    uint32_t exception_return;
    uint32_t stack_pointer;
    uint32_t frame_valid;

    fr_exception_frame_t frame;

    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t shcsr;
    uint32_t icsr;

    uint32_t mmfar;
    uint32_t bfar;
    uint32_t mmfar_valid;
    uint32_t bfar_valid;
} fr_fault_record_t;

extern volatile fr_fault_record_t g_fr_fault_record;

void fr_fault_init(void);

#endif