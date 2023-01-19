# Note that this file can be used as a top level CMakeLists.txt to build all the Demos (it includes
# them all, but they are usable in their own right, which is why no common work is done here)

cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "/home/charles/pi_pico/pico-sdk/")
# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

PROJECT(dallas_onewire)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(dallas_onewire
    dallas_onewire.c
)

pico_add_extra_outputs(dallas_onewire)

# link libraries
target_link_libraries(dallas_onewire 
    pico_stdlib 
    hardware_gpio
)


# enable usb output, disable uart output
pico_enable_stdio_usb(dallas_onewire 1)
pico_enable_stdio_uart(dallas_onewire 0)


# copy the output .uf2 file to the pico drive
# pico location /mnt/d/
add_custom_command(TARGET dallas_onewire POST_BUILD
    COMMAND COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/copy_to_pico.sh
    COMMENT "Copying to output directory"
)

