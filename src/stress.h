#ifndef FORGERTOS_DEMO_STRESS_H
#define FORGERTOS_DEMO_STRESS_H

#include <stdbool.h>
#include <stdint.h>

#define FR_DEMO_STRESS_PRODUCER_PRIORITY 5u
#define FR_DEMO_STRESS_CONSUMER_PRIORITY 4u
#define FR_DEMO_STRESS_WORKER_PRIORITY   2u
#define FR_DEMO_STRESS_QUEUE_CAPACITY    8u

typedef struct {
    bool initialized;
    bool started;

    uint32_t init_failures;
    uint32_t start_failures;
    uint32_t errors;

    uint32_t producer_task_id;
    uint32_t consumer_task_id;
    uint32_t worker_task_id;

    uint32_t producer_entry_count;
    uint32_t consumer_entry_count;
    uint32_t worker_entry_count;

    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t last_sent_sequence;
    uint32_t last_received_sequence;

    uint32_t queue_send_timeouts;
    uint32_t queue_receive_timeouts;
    uint32_t queue_depth;
    uint32_t queue_capacity;

    uint32_t order_errors;
    uint32_t checksum_errors;
    uint32_t latency_errors;
    uint32_t max_latency_ticks;

    uint32_t sync_requests;
    uint32_t sync_completions;
    uint32_t worker_locked_signals;

    uint32_t mutex_contentions;
    uint32_t mutex_acquisitions;
    uint32_t mutex_lock_timeouts;
    uint32_t worker_boost_observations;
    uint32_t worker_restore_observations;
    uint32_t worker_effective_priority;
    uint32_t worker_base_priority;
    uint32_t priority_errors;

    uint32_t ack_successes;
    uint32_t ack_timeouts;

    uint32_t worker_epoch;
    uint32_t timing_errors;

    uint32_t stack_checks;
    uint32_t stack_errors;
    uint32_t producer_min_free_words;
    uint32_t consumer_min_free_words;
    uint32_t worker_min_free_words;
} fr_demo_stress_status_t;

extern volatile fr_demo_stress_status_t g_fr_demo_stress_status;

bool fr_demo_stress_init(void);
bool fr_demo_stress_create_tasks(void);
bool fr_demo_stress_start(void);
bool fr_demo_stress_get_status(fr_demo_stress_status_t *out_status);
void fr_demo_stress_update_diagnostics(void);

#endif
