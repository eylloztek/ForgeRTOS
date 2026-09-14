#ifndef FORGE_ARCH_CONTEXT_PROBE_H
#define FORGE_ARCH_CONTEXT_PROBE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
} fr_arch_context_pattern_t;

bool fr_arch_context_probe_yield(const fr_arch_context_pattern_t *pattern);

_Static_assert(sizeof(fr_arch_context_pattern_t) == (8u * sizeof(uint32_t)),
               "Context probe pattern must contain exactly eight registers");

#endif