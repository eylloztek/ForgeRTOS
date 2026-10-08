#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "demo.h"
#include "forge/board.h"
#include "forge/critical.h"
#include "forge/queue.h"
#include "forge/scheduler.h"
#include "forge/task.h"
#include "forge/tick.h"

#define FR_RELEASE_DEMO_QUEUE_CAPACITY          8u
#define FR_RELEASE_DEMO_TASK_STACK_WORDS        128u
#define FR_RELEASE_DEMO_PRODUCER_PRIORITY       3u
#define FR_RELEASE_DEMO_CONSUMER_PRIORITY       4u
#define FR_RELEASE_DEMO_PRODUCER_PERIOD_TICKS   100u
#define FR_RELEASE_DEMO_LED_MESSAGE_INTERVAL    5u

typedef struct {
    uint32_t sequence;
    fr_tick_t produced_tick;
} fr_release_demo_message_t;

static fr_queue_t g_fr_release_demo_queue;
static fr_release_demo_message_t
    g_fr_release_demo_queue_storage[FR_RELEASE_DEMO_QUEUE_CAPACITY];

static _Alignas(8) uint32_t
    g_fr_release_demo_producer_stack[FR_RELEASE_DEMO_TASK_STACK_WORDS];
static _Alignas(8) uint32_t
    g_fr_release_demo_consumer_stack[FR_RELEASE_DEMO_TASK_STACK_WORDS];

static fr_task_handle_t g_fr_release_demo_producer_task;
static fr_task_handle_t g_fr_release_demo_consumer_task;
static volatile fr_release_demo_status_t g_fr_release_demo_status;

_Static_assert((FR_RELEASE_DEMO_TASK_STACK_WORDS % 2u) == 0u,
               "Release-demo task stacks must preserve 8-byte alignment");
_Static_assert(FR_RELEASE_DEMO_PRODUCER_PRIORITY <
               FR_RELEASE_DEMO_CONSUMER_PRIORITY,
               "Consumer must be able to preempt the producer");

static void fr_release_demo_record_send_error(void) {
    const fr_critical_state_t state = fr_critical_enter();
    ++g_fr_release_demo_status.send_errors;
    fr_critical_exit(state);
}

static void fr_release_demo_record_receive_error(void) {
    const fr_critical_state_t state = fr_critical_enter();
    ++g_fr_release_demo_status.receive_errors;
    fr_critical_exit(state);
}

static void fr_release_demo_producer_entry(void *argument) {
    (void)argument;

    uint32_t sequence = 0u;

    while (1) {
        fr_release_demo_message_t message;
        message.sequence = sequence + 1u;
        message.produced_tick = fr_tick_now();

        if (!fr_queue_send_wait(&g_fr_release_demo_queue,
                                &message,
                                FR_QUEUE_WAIT_FOREVER)) {
            fr_release_demo_record_send_error();
            continue;
        }

        sequence = message.sequence;

        const fr_critical_state_t state = fr_critical_enter();
        ++g_fr_release_demo_status.messages_sent;
        g_fr_release_demo_status.last_sent_sequence = sequence;
        g_fr_release_demo_status.queue_depth = g_fr_release_demo_queue.count;
        fr_critical_exit(state);

        if (!fr_task_sleep(FR_RELEASE_DEMO_PRODUCER_PERIOD_TICKS)) {
            fr_release_demo_record_send_error();
        }
    }
}

static void fr_release_demo_consumer_entry(void *argument) {
    (void)argument;

    uint32_t expected_sequence = 1u;

    while (1) {
        fr_release_demo_message_t message;

        if (!fr_queue_receive_wait(&g_fr_release_demo_queue,
                                   &message,
                                   FR_QUEUE_WAIT_FOREVER)) {
            fr_release_demo_record_receive_error();
            continue;
        }

        const uint32_t latency =
            fr_tick_elapsed(message.produced_tick, fr_tick_now());

        const fr_critical_state_t state = fr_critical_enter();

        ++g_fr_release_demo_status.messages_received;
        g_fr_release_demo_status.last_received_sequence = message.sequence;
        g_fr_release_demo_status.queue_depth = g_fr_release_demo_queue.count;

        if (message.sequence != expected_sequence) {
            ++g_fr_release_demo_status.order_errors;
            expected_sequence = message.sequence + 1u;
        } else {
            ++expected_sequence;
        }

        if (latency > g_fr_release_demo_status.max_latency_ticks) {
            g_fr_release_demo_status.max_latency_ticks = latency;
        }

        const uint32_t received =
            g_fr_release_demo_status.messages_received;

        fr_critical_exit(state);

        if ((received % FR_RELEASE_DEMO_LED_MESSAGE_INTERVAL) == 0u) {
            fr_board_led_toggle();
        }
    }
}

bool fr_release_demo_init(void) {
    if (!fr_queue_init(&g_fr_release_demo_queue,
                       g_fr_release_demo_queue_storage,
                       FR_RELEASE_DEMO_QUEUE_CAPACITY,
                       (uint32_t)sizeof(fr_release_demo_message_t))) {
        return false;
    }

    g_fr_release_demo_status.initialized = true;
    g_fr_release_demo_status.queue_depth = 0u;
    return true;
}

bool fr_release_demo_create_tasks(void) {
    if (!g_fr_release_demo_status.initialized) {
        ++g_fr_release_demo_status.task_create_errors;
        return false;
    }

    const fr_task_config_t producer_config = {
        .entry = fr_release_demo_producer_entry,
        .argument = NULL,
        .stack_memory = g_fr_release_demo_producer_stack,
        .stack_size_words = FR_RELEASE_DEMO_TASK_STACK_WORDS,
        .priority = FR_RELEASE_DEMO_PRODUCER_PRIORITY
    };

    const fr_task_config_t consumer_config = {
        .entry = fr_release_demo_consumer_entry,
        .argument = NULL,
        .stack_memory = g_fr_release_demo_consumer_stack,
        .stack_size_words = FR_RELEASE_DEMO_TASK_STACK_WORDS,
        .priority = FR_RELEASE_DEMO_CONSUMER_PRIORITY
    };

    if (fr_task_create(&g_fr_release_demo_producer_task,
                       &producer_config) != FR_TASK_OK) {
        ++g_fr_release_demo_status.task_create_errors;
        return false;
    }

    if (fr_task_create(&g_fr_release_demo_consumer_task,
                       &consumer_config) != FR_TASK_OK) {
        ++g_fr_release_demo_status.task_create_errors;
        return false;
    }

    fr_task_info_t info;

    if (!fr_task_get_info(g_fr_release_demo_producer_task, &info)) {
        ++g_fr_release_demo_status.task_create_errors;
        return false;
    }

    g_fr_release_demo_status.producer_task_id = info.id;

    if (!fr_task_get_info(g_fr_release_demo_consumer_task, &info)) {
        ++g_fr_release_demo_status.task_create_errors;
        return false;
    }

    g_fr_release_demo_status.consumer_task_id = info.id;
    return true;
}

bool fr_release_demo_get_status(fr_release_demo_status_t *out_status) {
    if (out_status == NULL) {
        return false;
    }

    const fr_critical_state_t state = fr_critical_enter();

    out_status->initialized = g_fr_release_demo_status.initialized;
    out_status->producer_task_id = g_fr_release_demo_status.producer_task_id;
    out_status->consumer_task_id = g_fr_release_demo_status.consumer_task_id;
    out_status->messages_sent = g_fr_release_demo_status.messages_sent;
    out_status->messages_received = g_fr_release_demo_status.messages_received;
    out_status->last_sent_sequence = g_fr_release_demo_status.last_sent_sequence;
    out_status->last_received_sequence = g_fr_release_demo_status.last_received_sequence;
    out_status->queue_depth = g_fr_release_demo_status.queue_depth;
    out_status->max_latency_ticks = g_fr_release_demo_status.max_latency_ticks;
    out_status->order_errors = g_fr_release_demo_status.order_errors;
    out_status->send_errors = g_fr_release_demo_status.send_errors;
    out_status->receive_errors = g_fr_release_demo_status.receive_errors;
    out_status->task_create_errors = g_fr_release_demo_status.task_create_errors;

    fr_critical_exit(state);
    return true;
}
