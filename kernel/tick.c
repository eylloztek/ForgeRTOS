#include "forge/kernel/tick_internal.h"
#include "forge/tick.h"

static volatile fr_tick_t g_fr_kernel_tick_count;

fr_tick_t fr_tick_now(void) {
    return g_fr_kernel_tick_count;
}

fr_tick_t fr_tick_elapsed(fr_tick_t start, fr_tick_t end) {
    return end - start;
}

bool fr_tick_has_elapsed(fr_tick_t start, fr_tick_t duration) {
    const fr_tick_t now = fr_tick_now();
    return fr_tick_elapsed(start, now) >= duration;
}

void fr_kernel_tick_isr(void) {
    ++g_fr_kernel_tick_count;
}

bool fr_tick_deadline_reached(fr_tick_t now, fr_tick_t deadline) {
    return (uint32_t)(now - deadline) <= (UINT32_MAX / 2u);
}