#include "forge/arch/cortex_m4.h"
#include "forge/board.h"

int main(void) {
    fr_arch_capture_boot_snapshot();

    fr_board_init();

    while (1) {
        fr_board_led_toggle();
        fr_board_delay_cycles(1000000u);
    }
}