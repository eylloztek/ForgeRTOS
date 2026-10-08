#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "demo.h"
#include "monitor.h"
#include "forge/arch/fault.h"
#include "forge/assert.h"
#include "forge/kernel_diagnostics.h"
#include "forge/task.h"
#include "forge/tick.h"
#include "forge/trace.h"
#include "forge/uart.h"
#include "forge/version.h"

#define FR_RELEASE_MONITOR_STACK_WORDS     128u
#define FR_RELEASE_MONITOR_PRIORITY        1u
#define FR_RELEASE_MONITOR_POLL_TICKS      50u
#define FR_RELEASE_MONITOR_LINE_CAPACITY   192u
#define FR_RELEASE_MONITOR_TRACE_COUNT     8u

static _Alignas(8) uint32_t
    g_fr_release_monitor_stack[FR_RELEASE_MONITOR_STACK_WORDS];
static fr_task_handle_t g_fr_release_monitor_task;
static char g_fr_release_monitor_line[FR_RELEASE_MONITOR_LINE_CAPACITY];
static fr_trace_entry_t
    g_fr_release_monitor_trace[FR_RELEASE_MONITOR_TRACE_COUNT];

_Static_assert((FR_RELEASE_MONITOR_STACK_WORDS % 2u) == 0u,
               "Monitor stack must preserve 8-byte alignment");

static uint32_t fr_release_text_length(const char *text) {
    uint32_t length = 0u;

    if (text == NULL) {
        return 0u;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

static bool fr_release_write_text(const char *text) {
    if (text == NULL) {
        return false;
    }

    return fr_uart_write((const uint8_t *)text,
                         fr_release_text_length(text));
}

static bool fr_release_line_append_char(uint32_t *length, char value) {
    if ((length == NULL) ||
        (*length >= (FR_RELEASE_MONITOR_LINE_CAPACITY - 1u))) {
        return false;
    }

    g_fr_release_monitor_line[*length] = value;
    ++(*length);
    g_fr_release_monitor_line[*length] = '\0';
    return true;
}

static bool fr_release_line_append_text(uint32_t *length,
                                        const char *text) {
    if ((length == NULL) || (text == NULL)) {
        return false;
    }

    for (uint32_t i = 0u; text[i] != '\0'; ++i) {
        if (!fr_release_line_append_char(length, text[i])) {
            return false;
        }
    }

    return true;
}

static bool fr_release_line_append_u32(uint32_t *length,
                                       uint32_t value) {
    char digits[10];
    uint32_t digit_count = 0u;

    if (value == 0u) {
        return fr_release_line_append_char(length, '0');
    }

    while ((value != 0u) && (digit_count < 10u)) {
        digits[digit_count] = (char)('0' + (value % 10u));
        value /= 10u;
        ++digit_count;
    }

    while (digit_count != 0u) {
        --digit_count;

        if (!fr_release_line_append_char(length, digits[digit_count])) {
            return false;
        }
    }

    return true;
}

static bool fr_release_line_append_hex_u32(uint32_t *length,
                                           uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";

    if (!fr_release_line_append_text(length, "0x")) {
        return false;
    }

    for (int32_t shift = 28; shift >= 0; shift -= 4) {
        if (!fr_release_line_append_char(
                length,
                digits[(value >> (uint32_t)shift) & 0xFu])) {
            return false;
        }
    }

    return true;
}

static const char *fr_release_trace_event_name(fr_trace_event_t event) {
    switch (event) {
        case FR_TRACE_EVENT_TASK_CREATE:
            return "CREATE";
        case FR_TRACE_EVENT_SCHEDULER_START:
            return "START";
        case FR_TRACE_EVENT_TASK_BLOCK:
            return "BLOCK";
        case FR_TRACE_EVENT_TASK_TIMEOUT:
            return "TIMEOUT";
        case FR_TRACE_EVENT_TASK_UNBLOCK:
            return "UNBLOCK";
        case FR_TRACE_EVENT_CONTEXT_SWITCH:
            return "SWITCH";
        default:
            return "UNKNOWN";
    }
}

static void fr_release_emit_help(void) {
    (void)fr_release_write_text(
        "Commands: h/? help, s status, k kernel, t trace, v version\r\n");
}

static void fr_release_emit_version(void) {
    (void)fr_release_write_text("ForgeRTOS v" FR_VERSION_STRING "\r\n");
}

static void fr_release_emit_status(void) {
    fr_release_demo_status_t demo;
    fr_trace_status_t trace;
    fr_uart_status_t uart;

    if (!fr_release_demo_get_status(&demo) ||
        !fr_trace_get_status(&trace) ||
        !fr_uart_get_status(&uart)) {
        (void)fr_release_write_text("ERR status unavailable\r\n");
        return;
    }

    uint32_t length = 0u;
    g_fr_release_monitor_line[0] = '\0';

    const bool built =
        fr_release_line_append_text(&length, "STATUS tick=") &&
        fr_release_line_append_u32(&length, fr_tick_now()) &&
        fr_release_line_append_text(&length, " tasks=") &&
        fr_release_line_append_u32(&length, fr_task_count()) &&
        fr_release_line_append_text(&length, " sent=") &&
        fr_release_line_append_u32(&length, demo.messages_sent) &&
        fr_release_line_append_text(&length, " recv=") &&
        fr_release_line_append_u32(&length, demo.messages_received) &&
        fr_release_line_append_text(&length, " q=") &&
        fr_release_line_append_u32(&length, demo.queue_depth) &&
        fr_release_line_append_text(&length, " latmax=") &&
        fr_release_line_append_u32(&length, demo.max_latency_ticks) &&
        fr_release_line_append_text(&length, " derr=") &&
        fr_release_line_append_u32(
            &length,
            demo.order_errors + demo.send_errors +
            demo.receive_errors + demo.task_create_errors) &&
        fr_release_line_append_text(&length, " trace=") &&
        fr_release_line_append_u32(&length, trace.count) &&
        fr_release_line_append_char(&length, '/') &&
        fr_release_line_append_u32(&length, trace.capacity) &&
        fr_release_line_append_text(&length, " ovw=") &&
        fr_release_line_append_u32(&length, trace.overwritten_events) &&
        fr_release_line_append_text(&length, " rxerr=") &&
        fr_release_line_append_u32(&length, uart.rx_error_count) &&
        fr_release_line_append_text(&length, "\r\n");

    if (!built ||
        !fr_uart_write((const uint8_t *)g_fr_release_monitor_line, length)) {
        (void)fr_release_write_text("ERR status format\r\n");
    }
}

static void fr_release_emit_kernel(void) {
    fr_kernel_invariant_report_t report;

    if (!fr_kernel_check_invariants(&report)) {
        /* The report still contains the violation details. */
    }

    uint32_t length = 0u;
    g_fr_release_monitor_line[0] = '\0';

    const bool built =
        fr_release_line_append_text(&length, "KERNEL tasks=") &&
        fr_release_line_append_u32(&length, report.task_count) &&
        fr_release_line_append_text(&length, " run=") &&
        fr_release_line_append_u32(&length, report.running_tasks) &&
        fr_release_line_append_text(&length, " ready=") &&
        fr_release_line_append_u32(&length, report.ready_tasks) &&
        fr_release_line_append_text(&length, " blocked=") &&
        fr_release_line_append_u32(&length, report.blocked_tasks) &&
        fr_release_line_append_text(&length, " current=") &&
        fr_release_line_append_u32(&length, report.current_task_id) &&
        fr_release_line_append_text(&length, " inv=") &&
        fr_release_line_append_hex_u32(&length, report.violation_mask) &&
        fr_release_line_append_text(&length, " assert=") &&
        fr_release_line_append_u32(&length, g_fr_assert_record.count) &&
        fr_release_line_append_text(&length, " fault=") &&
        fr_release_line_append_hex_u32(&length, g_fr_fault_record.magic) &&
        fr_release_line_append_text(&length, "\r\n");

    if (!built ||
        !fr_uart_write((const uint8_t *)g_fr_release_monitor_line, length)) {
        (void)fr_release_write_text("ERR kernel format\r\n");
    }
}

static void fr_release_emit_trace(void) {
    const uint32_t count =
        fr_trace_snapshot(g_fr_release_monitor_trace,
                          FR_RELEASE_MONITOR_TRACE_COUNT);

    if (!fr_release_write_text("TRACE newest events:\r\n")) {
        return;
    }

    for (uint32_t i = 0u; i < count; ++i) {
        const fr_trace_entry_t *const entry =
            &g_fr_release_monitor_trace[i];

        uint32_t length = 0u;
        g_fr_release_monitor_line[0] = '\0';

        const bool built =
            fr_release_line_append_char(&length, '#') &&
            fr_release_line_append_u32(&length, entry->sequence) &&
            fr_release_line_append_text(&length, " t=") &&
            fr_release_line_append_u32(&length, entry->tick) &&
            fr_release_line_append_char(&length, ' ') &&
            fr_release_line_append_text(
                &length,
                fr_release_trace_event_name(entry->event)) &&
            fr_release_line_append_text(&length, " task=") &&
            fr_release_line_append_u32(&length, entry->task_id) &&
            fr_release_line_append_text(&length, " a0=") &&
            fr_release_line_append_u32(&length, entry->arg0) &&
            fr_release_line_append_text(&length, " a1=") &&
            fr_release_line_append_u32(&length, entry->arg1) &&
            fr_release_line_append_text(&length, "\r\n");

        if (!built ||
            !fr_uart_write((const uint8_t *)g_fr_release_monitor_line,
                           length)) {
            break;
        }
    }
}

static void fr_release_process_command(uint8_t command) {
    if ((command == '\r') || (command == '\n')) {
        return;
    }

    switch (command) {
        case 'h':
        case '?':
            fr_release_emit_help();
            break;
        case 's':
            fr_release_emit_status();
            break;
        case 'k':
            fr_release_emit_kernel();
            break;
        case 't':
            fr_release_emit_trace();
            break;
        case 'v':
            fr_release_emit_version();
            break;
        default:
            (void)fr_release_write_text("ERR unknown command\r\n");
            break;
    }
}

static void fr_release_monitor_entry(void *argument) {
    (void)argument;

    (void)fr_release_write_text(
        "\r\nForgeRTOS v" FR_VERSION_STRING
        " release demo @ 115200 8-N-1\r\n");
    fr_release_emit_help();

    while (1) {
        uint8_t command;

        while (fr_uart_try_read(&command)) {
            fr_release_process_command(command);
        }

        if (!fr_task_sleep(FR_RELEASE_MONITOR_POLL_TICKS)) {
            (void)fr_release_write_text("ERR monitor sleep\r\n");
        }
    }
}

bool fr_release_monitor_create_task(void) {
    const fr_task_config_t config = {
        .entry = fr_release_monitor_entry,
        .argument = NULL,
        .stack_memory = g_fr_release_monitor_stack,
        .stack_size_words = FR_RELEASE_MONITOR_STACK_WORDS,
        .priority = FR_RELEASE_MONITOR_PRIORITY
    };

    return fr_task_create(&g_fr_release_monitor_task, &config) == FR_TASK_OK;
}
