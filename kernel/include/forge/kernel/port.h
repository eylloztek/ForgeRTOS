#ifndef FORGE_KERNEL_PORT_H
#define FORGE_KERNEL_PORT_H

#include <stdint.h>

#include "forge/task.h"

uint32_t *fr_port_task_stack_init(uint32_t *stack_top, fr_task_entry_t entry, void *argument);

void fr_port_scheduler_init(void);

_Noreturn void fr_port_start_first_task(void);
void fr_port_yield(void);

#endif