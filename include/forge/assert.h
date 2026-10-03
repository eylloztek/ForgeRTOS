#ifndef FORGE_ASSERT_H
#define FORGE_ASSERT_H

#include <stdint.h>

#ifndef FR_CONFIG_ENABLE_ASSERTS
#define FR_CONFIG_ENABLE_ASSERTS 1u
#endif

#define FR_ASSERT_RECORD_MAGIC 0x46524153u

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t line;
    uintptr_t file;
    uintptr_t expression;
    uint32_t ipsr;
    uint32_t control;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;
} fr_assert_record_t;

extern volatile fr_assert_record_t g_fr_assert_record;

void fr_assert_init(void);
_Noreturn void fr_assert_fail(const char *expression,
                              const char *file,
                              uint32_t line);

#if FR_CONFIG_ENABLE_ASSERTS
#define FR_ASSERT(expression)                                                \
    do {                                                                     \
        if (!(expression)) {                                                 \
            fr_assert_fail(#expression, __FILE__, (uint32_t)__LINE__);       \
        }                                                                    \
    } while (0)
#else
#define FR_ASSERT(expression) ((void)sizeof(expression))
#endif

#endif
