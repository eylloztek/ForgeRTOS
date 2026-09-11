#ifndef FORGE_SCHEDULER_H
#define FORGE_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#include "forge/task.h"

typedef uint8_t fr_scheduler_status_t;

enum {
    FR_SCHEDULER_OK = 0u,
    FR_SCHEDULER_ERROR_ALREADY_RUNNING,
    FR_SCHEDULER_ERROR_NO_READY_TASK
};

fr_scheduler_status_t fr_scheduler_start(void);
bool fr_scheduler_is_running(void);
fr_task_handle_t fr_scheduler_current_task(void);

#endif