#include <stdio.h>

#include "pico/stdlib.h"

int main() {
    stdio_init_all();

    // Wait 1s for uart
    sleep_ms(1000);

    printf("Hello, Raspberry Pi Pico!\n");

    // blink onboard led
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    while (true) {
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(500);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        sleep_ms(500);
    }
}