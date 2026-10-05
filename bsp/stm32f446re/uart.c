#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forge/critical.h"
#include "forge/uart.h"

#define FR_RCC_BASE             0x40023800u
#define FR_RCC_AHB1ENR          (*(volatile uint32_t *)(FR_RCC_BASE + 0x30u))
#define FR_RCC_APB1ENR          (*(volatile uint32_t *)(FR_RCC_BASE + 0x40u))

#define FR_GPIOA_BASE           0x40020000u
#define FR_GPIOA_MODER          (*(volatile uint32_t *)(FR_GPIOA_BASE + 0x00u))
#define FR_GPIOA_OTYPER         (*(volatile uint32_t *)(FR_GPIOA_BASE + 0x04u))
#define FR_GPIOA_OSPEEDR        (*(volatile uint32_t *)(FR_GPIOA_BASE + 0x08u))
#define FR_GPIOA_PUPDR          (*(volatile uint32_t *)(FR_GPIOA_BASE + 0x0Cu))
#define FR_GPIOA_AFRL           (*(volatile uint32_t *)(FR_GPIOA_BASE + 0x20u))

#define FR_USART2_BASE          0x40004400u
#define FR_USART2_SR            (*(volatile uint32_t *)(FR_USART2_BASE + 0x00u))
#define FR_USART2_DR            (*(volatile uint32_t *)(FR_USART2_BASE + 0x04u))
#define FR_USART2_BRR           (*(volatile uint32_t *)(FR_USART2_BASE + 0x08u))
#define FR_USART2_CR1           (*(volatile uint32_t *)(FR_USART2_BASE + 0x0Cu))
#define FR_USART2_CR2           (*(volatile uint32_t *)(FR_USART2_BASE + 0x10u))
#define FR_USART2_CR3           (*(volatile uint32_t *)(FR_USART2_BASE + 0x14u))

#define FR_RCC_AHB1ENR_GPIOAEN  (1u << 0)
#define FR_RCC_APB1ENR_USART2EN (1u << 17)

#define FR_GPIO_MODE_AF         2u
#define FR_GPIO_SPEED_HIGH      2u
#define FR_GPIO_PULL_NONE       0u
#define FR_GPIO_PULL_UP         1u
#define FR_GPIO_AF_USART2       7u

#define FR_USART_SR_PE          (1u << 0)
#define FR_USART_SR_FE          (1u << 1)
#define FR_USART_SR_NE          (1u << 2)
#define FR_USART_SR_ORE         (1u << 3)
#define FR_USART_SR_RXNE        (1u << 5)
#define FR_USART_SR_TC          (1u << 6)
#define FR_USART_SR_TXE         (1u << 7)

#define FR_USART_CR1_RE         (1u << 2)
#define FR_USART_CR1_TE         (1u << 3)
#define FR_USART_CR1_UE         (1u << 13)

#define FR_USART_ERROR_MASK \
    (FR_USART_SR_PE | FR_USART_SR_FE | FR_USART_SR_NE | FR_USART_SR_ORE)

static volatile bool g_fr_uart_initialized;
static volatile uint32_t g_fr_uart_peripheral_clock_hz;
static volatile uint32_t g_fr_uart_baud_rate;
static volatile uint32_t g_fr_uart_baud_divisor;
static volatile uint32_t g_fr_uart_tx_bytes;
static volatile uint32_t g_fr_uart_rx_bytes;
static volatile uint32_t g_fr_uart_rx_error_count;

static bool fr_uart_thread_context_allowed(void) {
    uint32_t ipsr;
    uint32_t primask;
    uint32_t faultmask;
    uint32_t basepri;

    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __asm volatile("mrs %0, faultmask" : "=r"(faultmask));
    __asm volatile("mrs %0, basepri" : "=r"(basepri));

    return (ipsr == 0u) &&
           ((primask & 1u) == 0u) &&
           ((faultmask & 1u) == 0u) &&
           (basepri == 0u);
}

static uint32_t fr_uart_calculate_brr(uint32_t peripheral_clock_hz,
                                     uint32_t baud_rate) {
    if ((peripheral_clock_hz == 0u) || (baud_rate == 0u)) {
        return 0u;
    }

    const uint32_t rounding = baud_rate / 2u;

    if (peripheral_clock_hz > (UINT32_MAX - rounding)) {
        return 0u;
    }

    const uint32_t divisor =
        (peripheral_clock_hz + rounding) / baud_rate;

    /*
     * USART2 uses oversampling by 16. A divisor below 16 is not valid for this
     * configuration, and BRR is a 16-bit register on STM32F446.
     */
    if ((divisor < 16u) || (divisor > 0xFFFFu)) {
        return 0u;
    }

    return divisor;
}

bool fr_uart_init(uint32_t peripheral_clock_hz, uint32_t baud_rate) {
    if (g_fr_uart_initialized) {
        return false;
    }

    const uint32_t baud_divisor =
        fr_uart_calculate_brr(peripheral_clock_hz, baud_rate);

    if (baud_divisor == 0u) {
        return false;
    }

    FR_RCC_AHB1ENR |= FR_RCC_AHB1ENR_GPIOAEN;
    FR_RCC_APB1ENR |= FR_RCC_APB1ENR_USART2EN;

    /* Ensure peripheral-clock enables have reached the bus before access. */
    (void)FR_RCC_AHB1ENR;
    (void)FR_RCC_APB1ENR;

    /* PA2 = USART2_TX, PA3 = USART2_RX, alternate function AF7. */
    FR_GPIOA_MODER =
        (FR_GPIOA_MODER & ~((3u << 4) | (3u << 6))) |
        (FR_GPIO_MODE_AF << 4) |
        (FR_GPIO_MODE_AF << 6);

    FR_GPIOA_OTYPER &= ~((1u << 2) | (1u << 3));

    FR_GPIOA_OSPEEDR =
        (FR_GPIOA_OSPEEDR & ~((3u << 4) | (3u << 6))) |
        (FR_GPIO_SPEED_HIGH << 4) |
        (FR_GPIO_SPEED_HIGH << 6);

    FR_GPIOA_PUPDR =
        (FR_GPIOA_PUPDR & ~((3u << 4) | (3u << 6))) |
        (FR_GPIO_PULL_NONE << 4) |
        (FR_GPIO_PULL_UP << 6);

    FR_GPIOA_AFRL =
        (FR_GPIOA_AFRL & ~((0xFu << 8) | (0xFu << 12))) |
        (FR_GPIO_AF_USART2 << 8) |
        (FR_GPIO_AF_USART2 << 12);

    /* 8 data bits, no parity, one stop bit, oversampling by 16. */
    FR_USART2_CR1 = 0u;
    FR_USART2_CR2 = 0u;
    FR_USART2_CR3 = 0u;
    FR_USART2_BRR = baud_divisor;

    /* Clear any stale receive/error condition before enabling the peripheral. */
    const uint32_t stale_status = FR_USART2_SR;
    const uint32_t stale_data = FR_USART2_DR;
    (void)stale_status;
    (void)stale_data;

    FR_USART2_CR1 = FR_USART_CR1_RE |
                    FR_USART_CR1_TE |
                    FR_USART_CR1_UE;

    g_fr_uart_peripheral_clock_hz = peripheral_clock_hz;
    g_fr_uart_baud_rate = baud_rate;
    g_fr_uart_baud_divisor = baud_divisor;
    g_fr_uart_tx_bytes = 0u;
    g_fr_uart_rx_bytes = 0u;
    g_fr_uart_rx_error_count = 0u;
    g_fr_uart_initialized = true;

    return true;
}

bool fr_uart_write(const uint8_t *data, uint32_t length) {
    if (!g_fr_uart_initialized ||
        (data == NULL) ||
        (length == 0u) ||
        !fr_uart_thread_context_allowed()) {
        return false;
    }

    for (uint32_t i = 0u; i < length; ++i) {
        while ((FR_USART2_SR & FR_USART_SR_TXE) == 0u) {
            /* Higher-priority RTOS tasks remain able to preempt this thread. */
        }

        FR_USART2_DR = data[i];
        ++g_fr_uart_tx_bytes;
    }

    while ((FR_USART2_SR & FR_USART_SR_TC) == 0u) {
        /* Wait until the final stop bit has left the peripheral. */
    }

    return true;
}

bool fr_uart_try_read(uint8_t *out_byte) {
    if (!g_fr_uart_initialized ||
        (out_byte == NULL) ||
        !fr_uart_thread_context_allowed()) {
        return false;
    }

    const uint32_t status = FR_USART2_SR;

    if ((status & FR_USART_ERROR_MASK) != 0u) {
        /* Reading DR after SR clears PE/FE/NE/ORE on STM32F4 USARTs. */
        const uint32_t discarded = FR_USART2_DR;
        (void)discarded;
        ++g_fr_uart_rx_error_count;
        return false;
    }

    if ((status & FR_USART_SR_RXNE) == 0u) {
        return false;
    }

    *out_byte = (uint8_t)FR_USART2_DR;
    ++g_fr_uart_rx_bytes;
    return true;
}

bool fr_uart_get_status(fr_uart_status_t *out_status) {
    if (out_status == NULL) {
        return false;
    }

    const fr_critical_state_t critical_state = fr_critical_enter();
    const uint32_t status = FR_USART2_SR;

    out_status->peripheral_clock_hz = g_fr_uart_peripheral_clock_hz;
    out_status->baud_rate = g_fr_uart_baud_rate;
    out_status->baud_divisor = g_fr_uart_baud_divisor;
    out_status->tx_bytes = g_fr_uart_tx_bytes;
    out_status->rx_bytes = g_fr_uart_rx_bytes;
    out_status->rx_error_count = g_fr_uart_rx_error_count;
    out_status->initialized = g_fr_uart_initialized;
    out_status->tx_ready = (status & FR_USART_SR_TXE) != 0u;
    out_status->rx_ready = (status & FR_USART_SR_RXNE) != 0u;

    fr_critical_exit(critical_state);
    return true;
}
