#ifndef FORGE_KERNEL_WAIT_INTERNAL_H
#define FORGE_KERNEL_WAIT_INTERNAL_H

#include <stdint.h>

#define FR_WAIT_FOREVER UINT32_MAX
#define FR_WAIT_MAX_FINITE_TICKS (UINT32_MAX / 2u)

typedef uint8_t fr_wait_reason_t;
typedef uint8_t fr_wait_result_t;

enum {
    FR_WAIT_REASON_NONE = 0u,
    FR_WAIT_REASON_SLEEP,
    FR_WAIT_REASON_SYNC,
    FR_WAIT_REASON_MUTEX
};

enum {
    FR_WAIT_RESULT_PENDING = 0u,
    FR_WAIT_RESULT_TIMEOUT,
    FR_WAIT_RESULT_SIGNALED
};

#endif