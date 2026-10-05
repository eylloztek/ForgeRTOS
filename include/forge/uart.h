#ifndef FORGE_UART_H
#define FORGE_UART_H

#include <stdbool.h>
#include <stdint.h>

#define FR_UART_DEFAULT_BAUD_RATE 115200u

typedef struct {
    uint32_t peripheral_clock_hz;
    uint32_t baud_rate;
    uint32_t baud_divisor;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t rx_error_count;
    bool initialized;
    bool tx_ready;
    bool rx_ready;
} fr_uart_status_t;

/*
 * Configure USART2 on PA2/PA3 for 8-N-1 operation. On the NUCLEO-F446RE,
 * these pins are connected to the ST-LINK virtual COM port by default.
 */
bool fr_uart_init(uint32_t peripheral_clock_hz, uint32_t baud_rate);

/*
 * Blocking transmit. This function is intended for Thread mode and must not be
 * called while PRIMASK, FAULTMASK, or BASEPRI masks are active. Higher-priority
 * ForgeRTOS tasks can still preempt the low-priority monitor task while it waits
 * for USART2 TXE/TC.
 */
bool fr_uart_write(const uint8_t *data, uint32_t length);

/* Poll USART2 once and return one received byte when available. */
bool fr_uart_try_read(uint8_t *out_byte);

/* Read UART configuration and diagnostic counters. */
bool fr_uart_get_status(fr_uart_status_t *out_status);

#endif
