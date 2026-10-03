#include <stdint.h>

#include "forge/assert.h"

volatile fr_assert_record_t g_fr_assert_record;

void fr_assert_init(void) {
    g_fr_assert_record.magic = 0u;
    g_fr_assert_record.count = 0u;
    g_fr_assert_record.line = 0u;
    g_fr_assert_record.file = (uintptr_t)0u;
    g_fr_assert_record.expression = (uintptr_t)0u;
    g_fr_assert_record.ipsr = 0u;
    g_fr_assert_record.control = 0u;
    g_fr_assert_record.primask = 0u;
    g_fr_assert_record.basepri = 0u;
    g_fr_assert_record.faultmask = 0u;
}

_Noreturn void fr_assert_fail(const char *expression,
                              const char *file,
                              uint32_t line) {
    uint32_t ipsr;
    uint32_t control;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;

    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    __asm volatile("mrs %0, control" : "=r"(control));
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __asm volatile("mrs %0, basepri" : "=r"(basepri));
    __asm volatile("mrs %0, faultmask" : "=r"(faultmask));

    __asm volatile("cpsid i" ::: "memory");

    const uint32_t next_count = g_fr_assert_record.count + 1u;

    /* Publish the magic last so a debugger never sees a partial valid record. */
    g_fr_assert_record.magic = 0u;
    g_fr_assert_record.count = next_count;
    g_fr_assert_record.line = line;
    g_fr_assert_record.file = (uintptr_t)file;
    g_fr_assert_record.expression = (uintptr_t)expression;
    g_fr_assert_record.ipsr = ipsr;
    g_fr_assert_record.control = control;
    g_fr_assert_record.primask = primask;
    g_fr_assert_record.basepri = basepri;
    g_fr_assert_record.faultmask = faultmask;

    __asm volatile("dsb" ::: "memory");
    g_fr_assert_record.magic = FR_ASSERT_RECORD_MAGIC;
    __asm volatile("dsb\nisb" ::: "memory");

    while (1) {
        __asm volatile("nop");
    }
}
