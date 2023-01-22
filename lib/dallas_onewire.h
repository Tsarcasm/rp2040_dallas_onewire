#ifndef DALLAS_ONEWIRE_H
#define DALLAS_ONEWIRE_H

#include <stdio.h>
#include <stdlib.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "malloc.h"
#include "pico/stdlib.h"
#include "string.h"

// Printf formatting for 64-bit roms
#define ROM_FORMAT "%02X%02X%02X%02X%02X%02X%02X%02X"
#define ROM_ARGS(rom) rom.bytes[0], rom.bytes[1], rom.bytes[2], rom.bytes[3], rom.bytes[4], rom.bytes[5], rom.bytes[6], rom.bytes[7]

// Printf formatting for 48-bit serial numbers
#define SN_FORMAT "%02X%02X%02X%02X%02X%02X"
#define SN_ARGS(rom) rom.serial_number[0], rom.serial_number[1], rom.serial_number[2], rom.serial_number[3], rom.serial_number[4], rom.serial_number[5]

#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
// Replace this with line below if you don't want debug prints
// #define DEBUG_PRINTF(...)

class DallasOneWire {
   public:
    // Rom layout
    // page 8: https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf
    union rom_t {
        struct {
            uint8_t family_code;
            uint8_t serial_number[6];
            uint8_t crc;
        } __attribute__((packed));
        uint8_t bytes[8];
        uint64_t uint64;
    };

    // Scratchpad layout
    // page 8 of datasheet
    union scratchpad_t {
        struct {
            uint8_t temperature[2];
            uint8_t high_alarm;
            uint8_t low_alarm;
            uint8_t configuration;
            uint8_t reserved[3];
            uint8_t crc;
        } __attribute__((packed));
        uint8_t bytes[9];
    } __attribute__((packed));

    // protocol functions
    // Send a reset pulse, and return true if a device responds with a presence pulse
    bool reset();
    void write_bit(bool bit);
    void write_byte(uint8_t byte);
    bool read_bit();
    uint8_t read_byte();

   protected:
    // PIO fifo wrapper functions
    void pio_fifo_put(uint32_t word);
    uint32_t pio_fifo_get();

    const PIO pio;
    uint sm;

    // Sensor rom array
    rom_t *sensors;
    size_t num_sensors;
    const size_t max_sensors;

    // Bus Search Algorithm
    // This code is adapted from the algorithm provided by Analog:
    //  - https://www.analog.com/en/app-notes/1wire-search-algorithm.html
    // Api: call Search() on a DallasOneWire object to repopulate the list of sensors
    // This will clear the sensors list, populate it the first 'max_sensors' sensors found on the bus
    // Then the list is sorted in ascending rom order
    class Search {
       private:
        int LastDiscrepancy;
        int LastFamilyDiscrepancy;
        bool LastDeviceFlag;
        uint8_t ROM_NO[8];

        DallasOneWire *wire;

        bool findNext();

       public:
        Search(DallasOneWire *wire);
    };

   public:
    // Constructor also initializes the state machine
    DallasOneWire(PIO pio, uint pin, size_t max_sensors = 8);

    // Get the number of sensors on the bus
    size_t getNumSensors();

    // Try and get the sensor at index (used for iterating over all sensors)
    bool tryGetSensor(size_t index, rom_t &sensor);

    // Update the list of sensors on the bus
    void updateSensors();

    // Instruct all sensors to perform a temperature conversion
    void convertTemperature();

    // Get the scratchpad of a sensor
    scratchpad_t readScratchpad(rom_t sensor);

    // Read the temperature of a sensor
    // Return true if the temperature was read successfully (false if crc error)
    bool readTemperature(rom_t sensor, float &temperature);

    static bool crc8(scratchpad_t scratchpad);
    static bool crc8(rom_t rom);
};

#endif  // DALLAS_ONEWIRE_H