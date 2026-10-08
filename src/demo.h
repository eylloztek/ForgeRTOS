#ifndef FORGE_RELEASE_DEMO_H
#define FORGE_RELEASE_DEMO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool initialized;
    uint32_t producer_task_id;
    uint32_t consumer_task_id;
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t last_sent_sequence;
    uint32_t last_received_sequence;
    uint32_t queue_depth;
    uint32_t max_latency_ticks;
    uint32_t order_errors;
    uint32_t send_errors;
    uint32_t receive_errors;
    uint32_t task_create_errors;
} fr_release_demo_status_t;

bool fr_release_demo_init(void);
bool fr_release_demo_create_tasks(void);
bool fr_release_demo_get_status(fr_release_demo_status_t *out_status);

#endif
