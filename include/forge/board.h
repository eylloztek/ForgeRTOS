#ifndef FORGE_BOARD_H
#define FORGE_BOARD_H

#include <stdint.h>

#define FR_BOARD_RESET_CORE_CLOCK_HZ 16000000u

void fr_board_init(void);
void fr_board_led_toggle(void);
void fr_board_delay_cycles(uint32_t cycles);

#endif
