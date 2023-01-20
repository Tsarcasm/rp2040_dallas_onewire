#include <stdio.h>

#include "blink.pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"
#include "malloc.h"
#include "pico/stdlib.h"
#include "string.h"

const uint CAPTURE_PIN_BASE = 15;
const uint CAPTURE_PIN_COUNT = 1;
const uint CAPTURE_N_SAMPLES = 50000;

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

static inline uint bits_packed_per_word(uint pin_count) {
    // If the number of pins to be sampled divides the shift register size, we
    // can use the full SR and FIFO width, and push when the input shift count
    // exactly reaches 32. If not, we have to push earlier, so we use the FIFO
    // a little less efficiently.
    const uint SHIFT_REG_WIDTH = 32;
    return SHIFT_REG_WIDTH - (SHIFT_REG_WIDTH % pin_count);
}

void logic_analyser_init(PIO pio, uint sm, uint pin_base, uint pin_count, float div) {
    // Load a program to capture n pins. This is just a single `in pins, n`
    // instruction with a wrap.
    uint16_t capture_prog_instr = pio_encode_in(pio_pins, pin_count);
    struct pio_program capture_prog = {
        .instructions = &capture_prog_instr,
        .length = 1,
        .origin = -1};
    uint offset = pio_add_program(pio, &capture_prog);

    // Configure state machine to loop over this `in` instruction forever,
    // with autopush enabled.
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_in_pins(&c, pin_base);
    sm_config_set_wrap(&c, offset, offset);
    sm_config_set_clkdiv(&c, div);
    // Note that we may push at a < 32 bit threshold if pin_count does not
    // divide 32. We are using shift-to-right, so the sample data ends up
    // left-justified in the FIFO in this case, with some zeroes at the LSBs.
    sm_config_set_in_shift(&c, true, true, bits_packed_per_word(pin_count));
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(pio, sm, offset, &c);
}

void logic_analyser_arm(PIO pio, uint sm, uint dma_chan, uint32_t *capture_buf, size_t capture_size_words,
                        uint trigger_pin, bool trigger_level) {
    pio_sm_set_enabled(pio, sm, false);
    // Need to clear _input shift counter_, as well as FIFO, because there may be
    // partial ISR contents left over from a previous run. sm_restart does this.
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);

    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));

    dma_channel_configure(dma_chan, &c,
                          capture_buf,         // Destination pointer
                          &pio->rxf[sm],       // Source pointer
                          capture_size_words,  // Number of transfers
                          true                 // Start immediately
    );

    pio_sm_exec(pio, sm, pio_encode_wait_gpio(trigger_level, trigger_pin));
    pio_sm_set_enabled(pio, sm, true);
}

void print_capture_buf(const uint32_t *buf, uint pin_base, uint pin_count, uint32_t n_samples) {
    // Display the capture buffer in text form, like this:
    // 00: __--__--__--__--__--__--
    // 01: ____----____----____----
    printf("Capture:\n");
    // Each FIFO record may be only partially filled with bits, depending on
    // whether pin_count is a factor of 32.
    uint record_size_bits = bits_packed_per_word(pin_count);
    for (int pin = 0; pin < pin_count; ++pin) {
        printf("%02d: ", pin + pin_base);
        for (int sample = 0; sample < n_samples; ++sample) {
            uint bit_index = pin + sample * pin_count;
            uint word_index = bit_index / record_size_bits;
            // Data is left-justified in each FIFO entry, hence the (32 - record_size_bits) offset
            uint word_mask = 1u << (bit_index % record_size_bits + 32 - record_size_bits);
            printf(buf[word_index] & word_mask ? "-" : "_");
        }
        printf("\n");
    }
}

void isr(uint gpio, uint32_t event_mask) {
    // uint32_t micros = time_us_32();
    // if (event_mask & GPIO_IRQ_EDGE_RISE) {
    //     printf("/ %d\r\n", micros);
    // } else if (event_mask & GPIO_IRQ_EDGE_FALL) {
    //     printf("\\ %d\r\n", micros);
    // }
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
    printf("Send ");
    for (int i = 0; i < 8; i++) {
        if (byte & (1 << i)) {
            write_1(pio, sm);
            printf("1");
        } else {
            write_0(pio, sm);
            printf("0");
        }
    }
    printf("\r\n");
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
    printf("Send Reset\r\n");
    pio_sm_put_blocking(pio, sm, 0b0001);
    uint32_t data = pio_sm_get_blocking(pio, sm);
    printf("Presence: 0x%08x\r\n", data);
}

int main() {
    stdio_init_all();

    const uint OUT_PIN = 14;
    const uint IN_PIN = 15;
    gpio_init(IN_PIN);
    gpio_set_dir(IN_PIN, GPIO_IN);

    // add interrupt
    gpio_set_irq_enabled_with_callback(IN_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &isr);

    // Wait 1s for uart
    sleep_ms(1000);

    printf("Hello, Raspberry Pi Pico!\n");

    static const uint led_pin = PICO_DEFAULT_LED_PIN;
    // We want to run the pio at 200kHz so we get 5uS per cycle
    static const float pio_freq = 200000;  // 500kHz
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

    // We're going to capture into a u32 buffer, for best DMA efficiency. Need
    // to be careful of rounding in case the number of pins being sampled
    // isn't a power of 2.
    uint total_sample_bits = CAPTURE_N_SAMPLES * CAPTURE_PIN_COUNT;
    total_sample_bits += bits_packed_per_word(CAPTURE_PIN_COUNT) - 1;
    uint buf_size_words = total_sample_bits / bits_packed_per_word(CAPTURE_PIN_COUNT);
    uint32_t *capture_buf = malloc(buf_size_words * sizeof(uint32_t));
    hard_assert(capture_buf);

    // Grant high bus priority to the DMA, so it can shove the processors out
    // of the way. This should only be needed if you are pushing things up to
    // >16bits/clk here, i.e. if you need to saturate the bus completely.
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    uint sm1 = pio_claim_unused_sm(pio, true);
    uint dma_chan = 0;

    // logic frequency: 1mhz
    static const float logic_freq = 1000000;
    // get clock divider
    uint logic_div = clock_get_hz(clk_sys) / logic_freq;

    logic_analyser_init(pio, sm1, CAPTURE_PIN_BASE, CAPTURE_PIN_COUNT, logic_div);

    while (true) {
        printf("Arming trigger\n");

        // logic_analyser_arm(pio, sm1, dma_chan, capture_buf, buf_size_words, CAPTURE_PIN_BASE, true);
        reset(pio, sm);
        writeByte(pio, sm, 0x33);  // read ROM
        uint8_t rom[8];
        printf("Rom code: ");
        for (int i = 0; i < 8; i++) {
            uint8_t byte = readByte(pio, sm);
            printf("%02x ", byte);
            rom[i] = byte;
        }
        bool crc_match = rom[7] == dallas_crc8(rom, 7);
        printf("CRC %s\r\n", crc_match ? "Match" : "Mismatch");

        reset(pio, sm);
        writeByte(pio, sm, 0xCC);  // skip ROM
        sleep_ms(100);
        writeByte(pio, sm, 0x44);  // convert T

        sleep_ms(1000);

        reset(pio, sm);
        writeByte(pio, sm, 0xCC);  // skip ROM
        sleep_ms(100);
        writeByte(pio, sm, 0xBE);  // get scratchpad
        sleep_ms(100);

        uint32_t data = 0;
        uint8_t bytes[9];
        for (int i = 0; i < 9; i++) {
            uint8_t byte = readByte(pio, sm);
            printf("%02x ", byte);
            bytes[i] = byte;
        }
        printf("\r\n", data);
        // get temp from bytes
        uint16_t raw = (bytes[1] << 8) | bytes[0];
        float temp = convert_temp_bytes(raw);
        int int_temp = (int)temp;
        crc_match = bytes[8] == dallas_crc8(bytes, 8);
        printf("temp: %d.%d (%s)\r\n", int_temp, (int)((temp - int_temp) * 1000), crc_match ? "CRC Match" : "CRC Mismatch");

        // dma_channel_wait_for_finish_blocking(dma_chan);
        // print_capture_buf(capture_buf, CAPTURE_PIN_BASE, CAPTURE_PIN_COUNT, CAPTURE_N_SAMPLES);
        sleep_ms(4000);
    }
}
