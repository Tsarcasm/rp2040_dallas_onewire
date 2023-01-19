#include <stdio.h>

#include "blink.pio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

int main() {
    stdio_init_all();

    // Wait 1s for uart
    sleep_ms(1000);

    printf("Hello, Raspberry Pi Pico!\n");

    static const uint led_pin = PICO_DEFAULT_LED_PIN;
    static const float pio_freq = 2000;  // 2kHz

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &blink_program);
    // get clock divider
    uint div = clock_get_hz(clk_sys) / pio_freq;
    // initialize program
    blink_program_init(pio, sm, offset, led_pin, div);
    // start program
    pio_sm_set_enabled(pio, sm, true);

    // do nothing
    while (true) {
        sleep_ms(1000);
    }
}