#include "dallas_onewire.h"

#include "d1w.pio.h"

///////////////////////
///  PIO interface  ///
///////////////////////

void DallasOneWire::pio_fifo_put(uint32_t word) {
    pio_sm_put_blocking(pio, sm, word);
}

uint32_t DallasOneWire::pio_fifo_get() {
    return pio_sm_get_blocking(pio, sm);
}

bool DallasOneWire::reset() {
    pio_fifo_put(0b0001);  // code 0b0001 is reset pulse
    uint32_t presence = pio_fifo_get();
    // sm returns 24 samples of the bus during the presence period
    // magic number: 0x00FFFFFF is no presence pulse (i.e. all high for the presence period)
    // anything less than this means we have a sensor pulling the line down for some time in that period
    return presence < 0x00FFFFFF;
}

void DallasOneWire::write_bit(bool bit) {
    if (bit) {
        pio_fifo_put(0b1111);  // code 0b1111 is write 1
    } else {
        pio_fifo_put(0b0111);  // code 0b0111 is write 0
    }
}

void DallasOneWire::write_byte(uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        if (byte & (1 << i)) {
            pio_fifo_put(0b1111);  // code 0b1111 is write 1
        } else {
            pio_fifo_put(0b0111);  // code 0b0111 is write 0
        }
    }
}

bool DallasOneWire::read_bit() {
    pio_fifo_put(0b0011);  // code 0b0011 is read bit
    uint32_t bit = pio_fifo_get();
    return bit != 0;
}

uint8_t DallasOneWire::read_byte() {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (read_bit() << i);
    }
    return byte;
}

///////////////////////
///       API       ///
///////////////////////

DallasOneWire::DallasOneWire(PIO pio, uint pin, size_t max_sensors) : pio(pio), max_sensors(max_sensors) {
    // Allocate memory for the sensors
    sensors = (rom_t *)malloc(max_sensors * sizeof(rom_t));
    num_sensors = 0;

    // Claim state machine
    sm = pio_claim_unused_sm(pio, true);
    // Setup clock
    static const float pio_freq = 200000;  // 200kHz (5us/cycle)
    uint offset = pio_add_program(pio, &d1w_program);
    // get clock divider
    uint div = clock_get_hz(clk_sys) / pio_freq;
    // initialize program
    d1w_program_init(pio, sm, offset, pin, div);
    // start state machine
    pio_sm_set_enabled(pio, sm, true);
    pio_sm_clear_fifos(pio, sm);
}

size_t DallasOneWire::getNumSensors() {
    return num_sensors;
}

bool DallasOneWire::tryGetSensor(size_t index, rom_t &sensor) {
    if (index >= num_sensors) {
        return false;
    }
    sensor = sensors[index];
    return true;
}

void DallasOneWire::updateSensors() {
    Search search(this);
}

void DallasOneWire::convertTemperature() {
    reset();
    write_byte(0xCC);  // skip rom
    write_byte(0x44);  // convert temperature
}

DallasOneWire::scratchpad_t DallasOneWire::readScratchpad(rom_t sensor) {
    reset();
    write_byte(0x55);  // match rom
    for (int i = 0; i < 8; i++) {
        write_byte(sensor.bytes[i]);
    }
    write_byte(0xBE);  // read scratchpad
    scratchpad_t scratchpad;
    for (int i = 0; i < 9; i++) {
        scratchpad.bytes[i] = read_byte();
    }

    if (!crc8(scratchpad)) {
        DEBUG_PRINTF("WARNING: Scratchpad CRC error!\r\n");
    }

    return scratchpad;
}

bool DallasOneWire::readTemperature(rom_t sensor, float &temperature) {
    scratchpad_t scratchpad = readScratchpad(sensor);
    if (!crc8(scratchpad)) {
        DEBUG_PRINTF("Scratchpad CRC error!\r\n");
        return false;
    }
    // Convert bytes to float
    // Format depends on chosen precision
    // (page 6) https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf
    uint32_t raw = (scratchpad.bytes[1] << 8) | scratchpad.bytes[0];
    float sign = (raw & 0x8000) ? -1 : 1;
    float temp = (raw >> 4) & 0x7FF;
    float frac = (raw & 0xF) / 16.0;
    temperature = sign * (temp + frac);
    return true;
}

static uint8_t dscrc_table[] = {
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

bool DallasOneWire::crc8(DallasOneWire::scratchpad_t scratchpad) {
    // Calculate CRC using the table
    uint8_t crc = 0;
    for (int i = 0; i < 8; i++) {
        crc = dscrc_table[crc ^ scratchpad.bytes[i]];
    }
    return crc == scratchpad.crc;
}

bool DallasOneWire::crc8(DallasOneWire::rom_t rom) {
    // Calculate CRC using the table
    uint8_t crc = 0;
    for (int i = 0; i < 7; i++) {
        crc = dscrc_table[crc ^ rom.bytes[i]];
    }

    return crc == rom.crc;
}