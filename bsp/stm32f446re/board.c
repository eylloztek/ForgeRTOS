#include "forge/board.h"

#include <stdint.h>

#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830u)
#define GPIOA_MODER (*(volatile uint32_t *)0x40020000u)
#define GPIOA_ODR   (*(volatile uint32_t *)0x40020014u)

#define RCC_AHB1ENR_GPIOAEN (1u << 0)
#define GPIOA_PIN5_MODE_MASK (3u << 10)
#define GPIOA_PIN5_OUTPUT    (1u << 10)
#define GPIOA_PIN5           (1u << 5)

void fr_board_init(void) {
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA_MODER = (GPIOA_MODER & ~GPIOA_PIN5_MODE_MASK) | GPIOA_PIN5_OUTPUT;
}

void fr_board_led_toggle(void) {
    GPIOA_ODR ^= GPIOA_PIN5;
}

void fr_board_delay_cycles(uint32_t cycles) {
    while (cycles-- > 0u) {
        __asm volatile("nop");
    }
}
