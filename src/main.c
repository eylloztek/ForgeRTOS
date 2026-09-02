#include "forge/board.h"

int main(void) {
    fr_board_init();

    while (1) {
        fr_board_led_toggle();
        fr_board_delay_cycles(1000000u);
    }
}
