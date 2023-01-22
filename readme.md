# RP2040 Dallas Onewire Library

This is a c++ library for the Raspberry Pi Pico implementing the dallas one-wire protocol using the PIO (Programmable IO) feature. I created this to move responsibility for fine timing away from the main application, intending to use this with FreeRTOS multitasking without critical sections.

Currently, this project has been tested with, and implements commands for [DS18B20](https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf) digital thermometer chips.

Example api use:

```cpp
    const uint DALLAS_PIN = 14;
    // Instantiate a d1w bus on pin 14
    DallasOneWire dallas(pio0, DALLAS_PIN, 8);
    
    // Find all sensors on the bus
    dallas.updateSensors();

    while(true) {
        printf("Getting temps...");
        dallas.convertTemperature();
        sleep_ms(1000);
        printf("done\r\n");

        // Get temps for each sensor
        for (uint i = 0; i < dallas.getNumSensors(); i++) {
            auto rom = dallas.getSensor(i);

            float t;
            if (!dallas.readTemperature(rom, t)) {
                printf("Failed to read temp on sensor %d\r\n", i);
                continue;
            } else {
                printf("Temp %d: %d.%d*C\r\n", i, (int)t, (int)(t * 10) % 10);
            }
        }

    }

```



