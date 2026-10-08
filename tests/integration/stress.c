#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/config.h"
#include "forge/critical.h"
#include "forge/event_flags.h"
#include "forge/mutex.h"
#include "forge/queue.h"
#include "forge/semaphore.h"
#include "forge/task.h"
#include "forge/task_stack.h"
#include "forge/tick.h"
#include "stress.h"

#define FR_DEMO_STRESS_START_BIT             (1u << 0)
#define FR_DEMO_STRESS_TASK_STACK_WORDS      128u
#define FR_DEMO_STRESS_QUEUE_TIMEOUT_TICKS   100u
#define FR_DEMO_STRESS_SYNC_TIMEOUT_TICKS    50u
#define FR_DEMO_STRESS_WORKER_HOLD_TICKS     2u
#define FR_DEMO_STRESS_SYNC_INTERVAL_MASK    0x0000001Fu
#define FR_DEMO_STRESS_LATENCY_LIMIT_TICKS   250u
#define FR_DEMO_STRESS_CHECKSUM_XOR          0x53545253u
#define FR_DEMO_STRESS_PAYLOAD_SEED          0xC001D00Du

_Static_assert(FR_CONFIG_MAX_TASKS >= 7u,
               "Stress validation requires capacity for seven user tasks");
_Static_assert((FR_DEMO_STRESS_TASK_STACK_WORDS % 2u) == 0u,
               "Stress task stacks must preserve 8-byte alignment");
_Static_assert(FR_DEMO_STRESS_WORKER_PRIORITY <
               FR_DEMO_STRESS_CONSUMER_PRIORITY,
               "Stress worker must be lower priority than the consumer");
_Static_assert(FR_DEMO_STRESS_CONSUMER_PRIORITY <
               FR_DEMO_STRESS_PRODUCER_PRIORITY,
               "Stress consumer must be lower priority than the producer");

typedef struct {
    uint32_t sequence;
    fr_tick_t produced_tick;
    uint32_t payload;
    uint32_t checksum;
} fr_demo_stress_message_t;

static fr_event_flags_t g_fr_demo_stress_start_flags;
static fr_queue_t g_fr_demo_stress_queue;
static fr_demo_stress_message_t
    g_fr_demo_stress_queue_storage[FR_DEMO_STRESS_QUEUE_CAPACITY];

static fr_counting_semaphore_t g_fr_demo_stress_sync_request;
static fr_binary_semaphore_t g_fr_demo_stress_worker_locked;
static fr_binary_semaphore_t g_fr_demo_stress_ack;
static fr_mutex_t g_fr_demo_stress_mutex;

static _Alignas(8) uint32_t
    g_fr_demo_stress_producer_stack[FR_DEMO_STRESS_TASK_STACK_WORDS];
static _Alignas(8) uint32_t
    g_fr_demo_stress_consumer_stack[FR_DEMO_STRESS_TASK_STACK_WORDS];
static _Alignas(8) uint32_t
    g_fr_demo_stress_worker_stack[FR_DEMO_STRESS_TASK_STACK_WORDS];

static fr_task_handle_t g_fr_demo_stress_producer_task;
static fr_task_handle_t g_fr_demo_stress_consumer_task;
static fr_task_handle_t g_fr_demo_stress_worker_task;

volatile fr_demo_stress_status_t g_fr_demo_stress_status;

static void fr_demo_stress_record_error(void) {
    const fr_critical_state_t critical_state = fr_critical_enter();
    ++g_fr_demo_stress_status.errors;
    fr_critical_exit(critical_state);
}

static uint32_t fr_demo_stress_checksum(
    const fr_demo_stress_message_t *message) {
    return message->sequence ^
           message->produced_tick ^
           message->payload ^
           FR_DEMO_STRESS_CHECKSUM_XOR;
}

static uint32_t fr_demo_stress_next_payload(uint32_t value) {
    return (value * 1664525u) + 1013904223u;
}

static bool fr_demo_stress_wait_for_start(void) {
    uint32_t matched = 0u;

    if (!fr_event_flags_wait(&g_fr_demo_stress_start_flags,
                             FR_DEMO_STRESS_START_BIT,
                             true,
                             false,
                             FR_EVENT_FLAGS_WAIT_FOREVER,
                             &matched)) {
        fr_demo_stress_record_error();
        return false;
    }

    if ((matched & FR_DEMO_STRESS_START_BIT) == 0u) {
        fr_demo_stress_record_error();
        return false;
    }

    return true;
}

static void fr_demo_stress_park(void) {
    while (1) {
        if (!fr_task_sleep(FR_TASK_SLEEP_MAX_TICKS)) {
            fr_demo_stress_record_error();
        }
    }
}

static void fr_demo_stress_run_sync_cycle(void) {
    if (!fr_counting_semaphore_give(&g_fr_demo_stress_sync_request)) {
        fr_demo_stress_record_error();
        return;
    }

    ++g_fr_demo_stress_status.sync_requests;

    if (!fr_binary_semaphore_take(&g_fr_demo_stress_worker_locked,
                                  FR_DEMO_STRESS_SYNC_TIMEOUT_TICKS)) {
        ++g_fr_demo_stress_status.mutex_lock_timeouts;
        fr_demo_stress_record_error();
        return;
    }

    ++g_fr_demo_stress_status.mutex_contentions;

    if (!fr_mutex_lock(&g_fr_demo_stress_mutex,
                       FR_DEMO_STRESS_SYNC_TIMEOUT_TICKS)) {
        ++g_fr_demo_stress_status.mutex_lock_timeouts;
        fr_demo_stress_record_error();
        return;
    }

    ++g_fr_demo_stress_status.mutex_acquisitions;

    if (g_fr_demo_stress_status.worker_epoch !=
        g_fr_demo_stress_status.sync_requests) {
        fr_demo_stress_record_error();
    }

    if (!fr_mutex_unlock(&g_fr_demo_stress_mutex)) {
        fr_demo_stress_record_error();
        return;
    }

    if (fr_binary_semaphore_take(&g_fr_demo_stress_ack,
                                 FR_DEMO_STRESS_SYNC_TIMEOUT_TICKS)) {
        ++g_fr_demo_stress_status.ack_successes;
    } else {
        ++g_fr_demo_stress_status.ack_timeouts;
        fr_demo_stress_record_error();
    }
}

static void fr_demo_stress_producer_entry(void *argument) {
    (void)argument;

    ++g_fr_demo_stress_status.producer_entry_count;

    if (!fr_demo_stress_wait_for_start()) {
        fr_demo_stress_park();
    }

    uint32_t payload_state = FR_DEMO_STRESS_PAYLOAD_SEED;
    uint32_t sequence = 0u;

    while (1) {
        fr_demo_stress_message_t message;

        payload_state = fr_demo_stress_next_payload(payload_state);

        message.sequence = sequence + 1u;
        message.produced_tick = fr_tick_now();
        message.payload = payload_state;
        message.checksum = fr_demo_stress_checksum(&message);

        if (!fr_queue_send_wait(&g_fr_demo_stress_queue,
                                &message,
                                FR_DEMO_STRESS_QUEUE_TIMEOUT_TICKS)) {
            ++g_fr_demo_stress_status.queue_send_timeouts;
            fr_demo_stress_record_error();
            continue;
        }

        sequence = message.sequence;
        ++g_fr_demo_stress_status.messages_sent;
        g_fr_demo_stress_status.last_sent_sequence = sequence;
        g_fr_demo_stress_status.queue_depth =
            g_fr_demo_stress_queue.count;

        if ((sequence & FR_DEMO_STRESS_SYNC_INTERVAL_MASK) == 0u) {
            fr_demo_stress_run_sync_cycle();
        }
    }
}

static void fr_demo_stress_consumer_entry(void *argument) {
    (void)argument;

    ++g_fr_demo_stress_status.consumer_entry_count;

    if (!fr_demo_stress_wait_for_start()) {
        fr_demo_stress_park();
    }

    uint32_t expected_sequence = 1u;

    while (1) {
        fr_demo_stress_message_t message;

        if (!fr_queue_receive_wait(&g_fr_demo_stress_queue,
                                   &message,
                                   FR_DEMO_STRESS_QUEUE_TIMEOUT_TICKS)) {
            ++g_fr_demo_stress_status.queue_receive_timeouts;
            fr_demo_stress_record_error();
            continue;
        }

        ++g_fr_demo_stress_status.messages_received;
        g_fr_demo_stress_status.last_received_sequence =
            message.sequence;
        g_fr_demo_stress_status.queue_depth =
            g_fr_demo_stress_queue.count;

        if (message.sequence != expected_sequence) {
            ++g_fr_demo_stress_status.order_errors;
            fr_demo_stress_record_error();
            expected_sequence = message.sequence + 1u;
        } else {
            ++expected_sequence;
        }

        if (message.checksum != fr_demo_stress_checksum(&message)) {
            ++g_fr_demo_stress_status.checksum_errors;
            fr_demo_stress_record_error();
        }

        const uint32_t latency =
            fr_tick_elapsed(message.produced_tick, fr_tick_now());

        if (latency > g_fr_demo_stress_status.max_latency_ticks) {
            g_fr_demo_stress_status.max_latency_ticks = latency;
        }

        if (latency > FR_DEMO_STRESS_LATENCY_LIMIT_TICKS) {
            ++g_fr_demo_stress_status.latency_errors;
            fr_demo_stress_record_error();
        }
    }
}

static void fr_demo_stress_worker_entry(void *argument) {
    (void)argument;

    ++g_fr_demo_stress_status.worker_entry_count;

    if (!fr_demo_stress_wait_for_start()) {
        fr_demo_stress_park();
    }

    while (1) {
        if (!fr_counting_semaphore_take(
                &g_fr_demo_stress_sync_request,
                FR_COUNTING_SEMAPHORE_WAIT_FOREVER)) {
            fr_demo_stress_record_error();
            continue;
        }

        if (!fr_mutex_lock(&g_fr_demo_stress_mutex,
                           FR_MUTEX_WAIT_FOREVER)) {
            fr_demo_stress_record_error();
            continue;
        }

        if (!fr_binary_semaphore_give(
                &g_fr_demo_stress_worker_locked)) {
            fr_demo_stress_record_error();
        } else {
            ++g_fr_demo_stress_status.worker_locked_signals;
        }

        /*
         * Giving worker_locked wakes the higher-priority producer. It then
         * blocks on this mutex and donates its priority before this worker
         * resumes. Verify that the inheritance is visible repeatedly.
         */
        fr_task_info_t priority_info;

        if (!fr_task_get_info(g_fr_demo_stress_worker_task,
                              &priority_info)) {
            ++g_fr_demo_stress_status.priority_errors;
            fr_demo_stress_record_error();
        } else {
            g_fr_demo_stress_status.worker_effective_priority =
                priority_info.priority;
            g_fr_demo_stress_status.worker_base_priority =
                priority_info.base_priority;

            if ((priority_info.priority ==
                 FR_DEMO_STRESS_PRODUCER_PRIORITY) &&
                (priority_info.base_priority ==
                 FR_DEMO_STRESS_WORKER_PRIORITY)) {
                ++g_fr_demo_stress_status.worker_boost_observations;
            } else {
                ++g_fr_demo_stress_status.priority_errors;
                fr_demo_stress_record_error();
            }
        }

        const fr_tick_t hold_start = fr_tick_now();

        if (!fr_task_sleep(FR_DEMO_STRESS_WORKER_HOLD_TICKS)) {
            ++g_fr_demo_stress_status.timing_errors;
            fr_demo_stress_record_error();
        } else {
            const uint32_t elapsed =
                fr_tick_elapsed(hold_start, fr_tick_now());

            if (elapsed < FR_DEMO_STRESS_WORKER_HOLD_TICKS) {
                ++g_fr_demo_stress_status.timing_errors;
                fr_demo_stress_record_error();
            }
        }

        ++g_fr_demo_stress_status.worker_epoch;
        ++g_fr_demo_stress_status.sync_completions;

        if (!fr_mutex_unlock(&g_fr_demo_stress_mutex)) {
            fr_demo_stress_record_error();
            continue;
        }

        if (!fr_task_get_info(g_fr_demo_stress_worker_task,
                              &priority_info)) {
            ++g_fr_demo_stress_status.priority_errors;
            fr_demo_stress_record_error();
        } else {
            g_fr_demo_stress_status.worker_effective_priority =
                priority_info.priority;
            g_fr_demo_stress_status.worker_base_priority =
                priority_info.base_priority;

            if ((priority_info.priority ==
                 FR_DEMO_STRESS_WORKER_PRIORITY) &&
                (priority_info.base_priority ==
                 FR_DEMO_STRESS_WORKER_PRIORITY)) {
                ++g_fr_demo_stress_status.worker_restore_observations;
            } else {
                ++g_fr_demo_stress_status.priority_errors;
                fr_demo_stress_record_error();
            }
        }

        if (!fr_binary_semaphore_give(&g_fr_demo_stress_ack)) {
            fr_demo_stress_record_error();
        }
    }
}

bool fr_demo_stress_init(void) {
    if (fr_task_count() != 0u) {
        ++g_fr_demo_stress_status.init_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    bool valid = true;

    if (!fr_event_flags_init(&g_fr_demo_stress_start_flags)) {
        valid = false;
    }

    if (!fr_queue_init(&g_fr_demo_stress_queue,
                       g_fr_demo_stress_queue_storage,
                       FR_DEMO_STRESS_QUEUE_CAPACITY,
                       (uint32_t)sizeof(fr_demo_stress_message_t))) {
        valid = false;
    }

    if (!fr_counting_semaphore_init(&g_fr_demo_stress_sync_request,
                                    0u,
                                    4u)) {
        valid = false;
    }

    if (!fr_binary_semaphore_init(&g_fr_demo_stress_worker_locked,
                                  false)) {
        valid = false;
    }

    if (!fr_binary_semaphore_init(&g_fr_demo_stress_ack, false)) {
        valid = false;
    }

    if (!fr_mutex_init(&g_fr_demo_stress_mutex)) {
        valid = false;
    }

    g_fr_demo_stress_status.queue_capacity =
        FR_DEMO_STRESS_QUEUE_CAPACITY;
    g_fr_demo_stress_status.queue_depth = 0u;
    g_fr_demo_stress_status.initialized = valid;

    if (!valid) {
        ++g_fr_demo_stress_status.init_failures;
        ++g_fr_demo_stress_status.errors;
    }

    return valid;
}

bool fr_demo_stress_create_tasks(void) {
    if (!g_fr_demo_stress_status.initialized ||
        (fr_task_count() > (FR_CONFIG_MAX_TASKS - 3u))) {
        ++g_fr_demo_stress_status.init_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    const fr_task_config_t producer_config = {
        .entry = fr_demo_stress_producer_entry,
        .argument = NULL,
        .stack_memory = g_fr_demo_stress_producer_stack,
        .stack_size_words = FR_DEMO_STRESS_TASK_STACK_WORDS,
        .priority = FR_DEMO_STRESS_PRODUCER_PRIORITY
    };

    const fr_task_config_t consumer_config = {
        .entry = fr_demo_stress_consumer_entry,
        .argument = NULL,
        .stack_memory = g_fr_demo_stress_consumer_stack,
        .stack_size_words = FR_DEMO_STRESS_TASK_STACK_WORDS,
        .priority = FR_DEMO_STRESS_CONSUMER_PRIORITY
    };

    const fr_task_config_t worker_config = {
        .entry = fr_demo_stress_worker_entry,
        .argument = NULL,
        .stack_memory = g_fr_demo_stress_worker_stack,
        .stack_size_words = FR_DEMO_STRESS_TASK_STACK_WORDS,
        .priority = FR_DEMO_STRESS_WORKER_PRIORITY
    };

    if (fr_task_create(&g_fr_demo_stress_producer_task,
                       &producer_config) != FR_TASK_OK) {
        ++g_fr_demo_stress_status.init_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    if (fr_task_create(&g_fr_demo_stress_consumer_task,
                       &consumer_config) != FR_TASK_OK) {
        ++g_fr_demo_stress_status.init_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    if (fr_task_create(&g_fr_demo_stress_worker_task,
                       &worker_config) != FR_TASK_OK) {
        ++g_fr_demo_stress_status.init_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    fr_task_info_t info;

    if (!fr_task_get_info(g_fr_demo_stress_producer_task, &info)) {
        ++g_fr_demo_stress_status.init_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    g_fr_demo_stress_status.producer_task_id = info.id;

    if (!fr_task_get_info(g_fr_demo_stress_consumer_task, &info)) {
        ++g_fr_demo_stress_status.init_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    g_fr_demo_stress_status.consumer_task_id = info.id;

    if (!fr_task_get_info(g_fr_demo_stress_worker_task, &info)) {
        ++g_fr_demo_stress_status.init_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    g_fr_demo_stress_status.worker_task_id = info.id;

    return true;
}

bool fr_demo_stress_start(void) {
    if (!g_fr_demo_stress_status.initialized ||
        g_fr_demo_stress_status.started) {
        ++g_fr_demo_stress_status.start_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    if (!fr_event_flags_set(&g_fr_demo_stress_start_flags,
                            FR_DEMO_STRESS_START_BIT)) {
        ++g_fr_demo_stress_status.start_failures;
        ++g_fr_demo_stress_status.errors;
        return false;
    }

    g_fr_demo_stress_status.started = true;
    return true;
}

void fr_demo_stress_update_diagnostics(void) {
    fr_task_stack_info_t producer;
    fr_task_stack_info_t consumer;
    fr_task_stack_info_t worker;

    const bool valid =
        fr_task_stack_get_info(g_fr_demo_stress_producer_task,
                               &producer) &&
        fr_task_stack_get_info(g_fr_demo_stress_consumer_task,
                               &consumer) &&
        fr_task_stack_get_info(g_fr_demo_stress_worker_task,
                               &worker);

    if (!valid ||
        !producer.guard_intact ||
        !consumer.guard_intact ||
        !worker.guard_intact ||
        producer.overflow_detected ||
        consumer.overflow_detected ||
        worker.overflow_detected ||
        (producer.minimum_usable_free_words == 0u) ||
        (consumer.minimum_usable_free_words == 0u) ||
        (worker.minimum_usable_free_words == 0u)) {
        ++g_fr_demo_stress_status.stack_errors;
        fr_demo_stress_record_error();
    } else {
        g_fr_demo_stress_status.producer_min_free_words =
            producer.minimum_usable_free_words;
        g_fr_demo_stress_status.consumer_min_free_words =
            consumer.minimum_usable_free_words;
        g_fr_demo_stress_status.worker_min_free_words =
            worker.minimum_usable_free_words;
    }

    ++g_fr_demo_stress_status.stack_checks;
}

bool fr_demo_stress_get_status(fr_demo_stress_status_t *out_status) {
    if (out_status == NULL) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();

    g_fr_demo_stress_status.queue_depth = g_fr_demo_stress_queue.count;

    out_status->initialized = g_fr_demo_stress_status.initialized;
    out_status->started = g_fr_demo_stress_status.started;
    out_status->init_failures = g_fr_demo_stress_status.init_failures;
    out_status->start_failures = g_fr_demo_stress_status.start_failures;
    out_status->errors = g_fr_demo_stress_status.errors;

    out_status->producer_task_id = g_fr_demo_stress_status.producer_task_id;
    out_status->consumer_task_id = g_fr_demo_stress_status.consumer_task_id;
    out_status->worker_task_id = g_fr_demo_stress_status.worker_task_id;

    out_status->producer_entry_count =
        g_fr_demo_stress_status.producer_entry_count;
    out_status->consumer_entry_count =
        g_fr_demo_stress_status.consumer_entry_count;
    out_status->worker_entry_count =
        g_fr_demo_stress_status.worker_entry_count;

    out_status->messages_sent = g_fr_demo_stress_status.messages_sent;
    out_status->messages_received = g_fr_demo_stress_status.messages_received;
    out_status->last_sent_sequence =
        g_fr_demo_stress_status.last_sent_sequence;
    out_status->last_received_sequence =
        g_fr_demo_stress_status.last_received_sequence;

    out_status->queue_send_timeouts =
        g_fr_demo_stress_status.queue_send_timeouts;
    out_status->queue_receive_timeouts =
        g_fr_demo_stress_status.queue_receive_timeouts;
    out_status->queue_depth = g_fr_demo_stress_status.queue_depth;
    out_status->queue_capacity = g_fr_demo_stress_status.queue_capacity;

    out_status->order_errors = g_fr_demo_stress_status.order_errors;
    out_status->checksum_errors = g_fr_demo_stress_status.checksum_errors;
    out_status->latency_errors = g_fr_demo_stress_status.latency_errors;
    out_status->max_latency_ticks =
        g_fr_demo_stress_status.max_latency_ticks;

    out_status->sync_requests = g_fr_demo_stress_status.sync_requests;
    out_status->sync_completions = g_fr_demo_stress_status.sync_completions;
    out_status->worker_locked_signals =
        g_fr_demo_stress_status.worker_locked_signals;

    out_status->mutex_contentions =
        g_fr_demo_stress_status.mutex_contentions;
    out_status->mutex_acquisitions =
        g_fr_demo_stress_status.mutex_acquisitions;
    out_status->mutex_lock_timeouts =
        g_fr_demo_stress_status.mutex_lock_timeouts;
    out_status->worker_boost_observations =
        g_fr_demo_stress_status.worker_boost_observations;
    out_status->worker_restore_observations =
        g_fr_demo_stress_status.worker_restore_observations;
    out_status->worker_effective_priority =
        g_fr_demo_stress_status.worker_effective_priority;
    out_status->worker_base_priority =
        g_fr_demo_stress_status.worker_base_priority;
    out_status->priority_errors =
        g_fr_demo_stress_status.priority_errors;

    out_status->ack_successes = g_fr_demo_stress_status.ack_successes;
    out_status->ack_timeouts = g_fr_demo_stress_status.ack_timeouts;

    out_status->worker_epoch = g_fr_demo_stress_status.worker_epoch;
    out_status->timing_errors = g_fr_demo_stress_status.timing_errors;

    out_status->stack_checks = g_fr_demo_stress_status.stack_checks;
    out_status->stack_errors = g_fr_demo_stress_status.stack_errors;
    out_status->producer_min_free_words =
        g_fr_demo_stress_status.producer_min_free_words;
    out_status->consumer_min_free_words =
        g_fr_demo_stress_status.consumer_min_free_words;
    out_status->worker_min_free_words =
        g_fr_demo_stress_status.worker_min_free_words;

    fr_critical_exit(critical_state);
    return true;
}
