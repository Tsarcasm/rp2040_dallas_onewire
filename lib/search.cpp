#include "dallas_onewire.h"

///////////////////////
///  Search Funcs   ///
///////////////////////

DallasOneWire::Search::Search(DallasOneWire *d1w) {
    this->wire = d1w;
    // Reset search state
    this->LastDiscrepancy = 0;
    this->LastDeviceFlag = false;
    this->LastFamilyDiscrepancy = 0;
    rom_t rom = {0};

    // Clear the sensors list
    d1w->num_sensors = 0;

    while (findNext()) {
        // New sensor is in d1w->rom
        if (d1w->num_sensors >= d1w->max_sensors) {
            // Too many sensors
            DEBUG_PRINTF("D1W_SEARCH: Found more sensors than we have space for\r\n");
            break;
        }
        
        memcpy(&rom, ROM_NO, 8);

        if (DallasOneWire::crc8(rom) != true) {
            // CRC error
            DEBUG_PRINTF("D1W_SEARCH: CRC error\r\n");
            continue;
        }

        // Add sensor to list
        d1w->sensors[d1w->num_sensors++].uint64 = rom.uint64;
    }

    // Sort the sensors list
    // Bubble sort for now (lol) //TODO: improve this
    for (size_t i = 0; i < d1w->num_sensors; i++) {
        for (size_t j = i + 1; j < d1w->num_sensors; j++) {
            if (d1w->sensors[i].uint64 > d1w->sensors[j].uint64) {
                // Swap
                rom_t tmp = d1w->sensors[i];
                d1w->sensors[i] = d1w->sensors[j];
                d1w->sensors[j] = tmp;
            }
        }
    }
}


// Again, this is a version of the example search provided by Analog.
// Not going to touch this too much
bool DallasOneWire::Search::findNext() {
    int id_bit_number;
    int last_zero, rom_byte_number, search_result;
    int id_bit, cmp_id_bit;
    unsigned char rom_byte_mask, search_direction;

    // initialize for search
    id_bit_number = 1;
    last_zero = 0;
    rom_byte_number = 0;
    rom_byte_mask = 1;
    search_result = 0;
    // if the last call was not the last one
    if (!LastDeviceFlag) {
        // 1-Wire reset
        if (!wire->reset()) {
            // reset the search
            LastDiscrepancy = 0;
            LastDeviceFlag = false;
            LastFamilyDiscrepancy = 0;
            return false;
        }

        // issue the search command
        wire->write_byte(0xF0);

        // loop to do the search
        do {
            // read a bit and its complement
            id_bit = (int)wire->read_bit();
            cmp_id_bit = (int)wire->read_bit();

            // check for no devices on 1-wire
            if ((id_bit == 1) && (cmp_id_bit == 1))
                break;
            else {
                // all devices coupled have 0 or 1
                if (id_bit != cmp_id_bit)
                    search_direction = id_bit;  // bit write value for search
                else {
                    // if this discrepancy if before the Last Discrepancy
                    // on a previous next then pick the same as last time
                    if (id_bit_number < LastDiscrepancy)
                        search_direction = ((ROM_NO[rom_byte_number] & rom_byte_mask) > 0);
                    else
                        // if equal to last pick 1, if not then pick 0
                        search_direction = (id_bit_number == LastDiscrepancy);

                    // if 0 was picked then record its position in LastZero
                    if (search_direction == 0) {
                        last_zero = id_bit_number;

                        // check for Last discrepancy in family
                        if (last_zero < 9)
                            LastFamilyDiscrepancy = last_zero;
                    }
                }

                // set or clear the bit in the ROM byte rom_byte_number
                // with mask rom_byte_mask
                if (search_direction == 1)
                    ROM_NO[rom_byte_number] |= rom_byte_mask;
                else
                    ROM_NO[rom_byte_number] &= ~rom_byte_mask;

                // serial number search direction write bit
                wire->write_bit(search_direction);

                // increment the byte counter id_bit_number
                // and shift the mask rom_byte_mask
                id_bit_number++;
                rom_byte_mask <<= 1;

                // if the mask is 0 then go to new SerialNum byte rom_byte_number and reset mask
                if (rom_byte_mask == 0) {
                    rom_byte_number++;
                    rom_byte_mask = 1;
                }
            }
        } while (rom_byte_number < 8);  // loop until through all ROM bytes 0-7

        // if the search was successful then
        if (!(id_bit_number < 65)) {
            // search successful so set LastDiscrepancy,LastDeviceFlag,search_result
            LastDiscrepancy = last_zero;

            // check for last device
            if (LastDiscrepancy == 0)
                LastDeviceFlag = true;

            search_result = true;
        }
    }

    // if no device found then reset counters so next 'search' will be like a first
    if (!search_result || !ROM_NO[0]) {
        LastDiscrepancy = 0;
        LastDeviceFlag = false;
        LastFamilyDiscrepancy = 0;
        search_result = false;
    }

    return search_result;
}
