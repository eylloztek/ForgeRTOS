#ifndef FORGE_CRITICAL_H
#define FORGE_CRITICAL_H

#include <stdint.h>

typedef uint32_t fr_critical_state_t;

fr_critical_state_t fr_critical_enter(void);
void fr_critical_exit(fr_critical_state_t state);

#endif