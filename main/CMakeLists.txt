cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_emb C CXX ASM)

pico_sdk_init()

add_executable(pico_emb
    main.c
    hardware.c
    audio.c
    jogo.c
)

set_target_properties(pico_emb PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

pico_enable_stdio_usb(pico_emb 1)
pico_enable_stdio_uart(pico_emb 0)

target_include_directories(pico_emb PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pico_emb
    pico_stdlib
    pico_multicore
    hardware_pwm
    hardware_irq
    hardware_clocks
    hardware_sync
    hardware_gpio
    hardware_timer
)

pico_add_extra_outputs(pico_emb)
