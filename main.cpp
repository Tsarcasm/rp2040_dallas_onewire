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

const uint CAPTURE_PIN_BASE = 15;
const uint CAPTURE_PIN_COUNT = 1;
const uint CAPTURE_N_SAMPLES = 50000;

#define SK6812_PIN 16

static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(r) << 8) |
           ((uint32_t)(g) << 16) |
           (uint32_t)(b);
}

static unsigned char dscrc_table[] = {
    0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
    157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
    35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
    190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
    70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
    219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
    101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
    248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
    140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
    17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
    175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
    50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
    202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
    87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
    233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
    116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53};

uint8_t dallas_crc8(uint8_t *dat, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc = dscrc_table[crc ^ dat[i]];
    }
    return crc;
}

void write_0(PIO pio, uint sm) {
    pio_sm_put_blocking(pio, sm, 0b0111);
}

void write_1(PIO pio, uint sm) {
    pio_sm_put_blocking(pio, sm, 0b1111);
}

bool read_bit(PIO pio, uint sm) {
    pio_sm_put_blocking(pio, sm, 0b0011);
    uint32_t data = pio_sm_get_blocking(pio, sm);
    return data != 0;
}

void writeByte(PIO pio, uint sm, uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        if (byte & (1 << i)) {
            write_1(pio, sm);
        } else {
            write_0(pio, sm);
        }
    }
}

uint8_t readByte(PIO pio, uint sm) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        if (read_bit(pio, sm)) {
            byte |= (1 << i);
        }
    }
    return byte;
}

float convert_temp_bytes(uint16_t raw) {
    float sign = (raw & 0x8000) ? -1 : 1;
    float temp = (raw >> 4) & 0x7FF;
    float frac = (raw & 0xF) / 16.0;
    return sign * (temp + frac);
}

void reset(PIO pio, uint sm) {
    // printf("Send Reset\r\n");
    pio_sm_put_blocking(pio, sm, 0b0001);
    uint32_t data = pio_sm_get_blocking(pio, sm);
    // printf("Presence: 0x%08x\r\n", data);
}

int main() {
    stdio_init_all();

    const uint DALLAS_PIN = 14;

    // Wait 1s for uart
    sleep_ms(1000);

    // printf("Hello, Raspberry Pi Pico!\n");

    static const uint led_pin = PICO_DEFAULT_LED_PIN;

    // todo get free sm
    PIO pio_pixel = pio0;
    int sm_pixel = pio_claim_unused_sm(pio_pixel, true);
    uint offset_pixel = pio_add_program(pio_pixel, &sk6812_program);

    sk6812_program_init(pio_pixel, sm_pixel, offset_pixel, SK6812_PIN, 800000, false);
    put_pixel(pio_pixel, sm_pixel, urgb_u32(0, 0, 0));

    DallasOneWire dallas(pio0, DALLAS_PIN, 1);

    while (true) {
        printf("Searching for devices\r\n");
        bool p = dallas.reset();
        printf("Presence: %d\r\n", p);
        dallas.updateSensors();
        for (uint i = 0; i < dallas.getNumSensors(); i++) {
            DallasOneWire::rom_t rom;
            if (!dallas.tryGetSensor(i, rom)) break;
            printf("Found rom %d: 0x" SN_FORMAT "\r\n", i, SN_ARGS(rom));
        }
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
        sleep_ms(1000);
    }

    while (true) {
        // Pulse through all 3 colours
        uint8_t r = rand() % 255;
        bool rdir = rand() % 2;
        uint8_t g = rand() % 255;
        bool gdir = rand() % 2;
        uint8_t b = rand() % 255;
        bool bdir = rand() % 2;
        for (int i = 0; i < 1000; i++) {
            r = rdir ? (r + 1) % 255 : (r - 1) % 255;
            g = gdir ? (g + 1) % 255 : (g - 1) % 255;
            b = bdir ? (b + 1) % 255 : (b - 1) % 255;
            put_pixel(pio_pixel, sm_pixel, urgb_u32(r / 2, g / 10, b / 2));
            sleep_ms(10);
        }
    }
}