#include <stdio.h>
#include <stdlib.h>

#include "dallas_onewire.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"
#include "malloc.h"
#include "pico/stdlib.h"
#include "sk6812.pio.h"
#include "string.h"

#define SK6812_PIN 16
const PIO sk_pio = pio0;
int sk_sm = 0;
static inline void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(sk_pio, sk_sm, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(r) << 8) |
           ((uint32_t)(g) << 16) |
           (uint32_t)(b);
}
int main() {
    stdio_init_all();

    const uint DALLAS_PIN = 14;

    // Wait 1s for uart
    sleep_ms(1000);

    static const uint led_pin = PICO_DEFAULT_LED_PIN;

    // todo get free sm

    sk_sm = pio_claim_unused_sm(sk_pio, true);
    uint offset_pixel = pio_add_program(sk_pio, &sk6812_program);

    sk6812_program_init(sk_pio, sk_sm, offset_pixel, SK6812_PIN, 800000, false);
    put_pixel(urgb_u32(0, 0, 0));

    DallasOneWire dallas(pio0, DALLAS_PIN, 8);
    while (true) {
        put_pixel(urgb_u32(25, 25, 0));
        printf("Searching for devices\r\n");
        dallas.updateSensors();
        for (uint i = 0; i < dallas.getNumSensors(); i++) {
            DallasOneWire::rom_t rom;
            if (!dallas.tryGetSensor(i, rom)) break;
            printf("Found rom %d: 0x" SN_FORMAT "\r\n", i, SN_ARGS(rom));
        }
        put_pixel(urgb_u32(0, 0, 25));
        dallas.convertTemperature();
        sleep_ms(1000);
        for (uint i = 0; i < dallas.getNumSensors(); i++) {
            DallasOneWire::rom_t rom;
            if (!dallas.tryGetSensor(i, rom)) break;
            float t;
            if (!dallas.readTemperature(rom, t)) {
                printf("Failed to read temperature for rom %d: 0x" SN_FORMAT "\r\n", i, SN_ARGS(rom));
                continue;
            } else {
                printf("T %d: %d.%d\r\n", i, (int)t, (int)(t * 10) % 10);
            }
        }
        put_pixel(urgb_u32(0, 50, 5));
        sleep_ms(1000);
    }

    // while (true) {
    //     // Pulse through all 3 colours
    //     uint8_t r = rand() % 255;
    //     bool rdir = rand() % 2;
    //     uint8_t g = rand() % 255;
    //     bool gdir = rand() % 2;
    //     uint8_t b = rand() % 255;
    //     bool bdir = rand() % 2;
    //     for (int i = 0; i < 1000; i++) {
    //         r = rdir ? (r + 1) % 255 : (r - 1) % 255;
    //         g = gdir ? (g + 1) % 255 : (g - 1) % 255;
    //         b = bdir ? (b + 1) % 255 : (b - 1) % 255;
    //         put_pixel(pio_pixel, sm_pixel, urgb_u32(r / 2, g / 10, b / 2));
    //         sleep_ms(10);
    //     }
    // }
}