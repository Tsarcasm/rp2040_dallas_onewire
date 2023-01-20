#include <stdio.h>

#include "blink.pio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

void isr(uint gpio, uint32_t event_mask) {
    if (event_mask & GPIO_IRQ_EDGE_RISE) {
        printf("0\r\n");
        printf("1\r\n");
    } else {
        printf("1\r\n");
        printf("0\r\n");
    }
}

int main() {
    stdio_init_all();

    const uint OUT_PIN = 14;
    // gpio_init(OUT_PIN);
    // gpio_set_dir(OUT_PIN, GPIO_OUT);

    const uint IN_PIN = 15;
    gpio_init(IN_PIN);
    gpio_set_dir(IN_PIN, GPIO_IN);

    // add interrupt
    gpio_set_irq_enabled_with_callback(IN_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &isr);

    // Wait 1s for uart
    sleep_ms(1000);

    printf("Hello, Raspberry Pi Pico!\n");

    static const uint led_pin = PICO_DEFAULT_LED_PIN;
    // We want to run the pio at 500kHz, so we get 2uS per cycle
    static const float pio_freq = 500000;  // 500kHz
    // static const float pio_freq = 100000;  // 2kHz

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &blink_program);
    // get clock divider
    uint div = clock_get_hz(clk_sys) / pio_freq;
    // initialize program
    blink_program_init(pio, sm, offset, OUT_PIN, div);
    // start program
    pio_sm_set_enabled(pio, sm, true);
    // do nothing

    pio_sm_clear_fifos(pio, sm);

    while (true) {
        printf("send 0b1000\r\n");
        // if fifo full
        if (pio_sm_is_tx_fifo_full(pio, sm)) {
            printf("fifo full\r\n");
            // what's the program counter?
        }
        uint32_t pc = pio_sm_get_pc(pio, sm);
        printf("pc: %x\r\n", pc);
        pio_sm_put_blocking(pio, sm, 0b0001);

        sleep_ms(500);
        while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
            uint32_t data = pio_sm_get(pio, sm);
            printf("data: %x\r\n", data);
        }
    }
}