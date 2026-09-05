cmake_minimum_required(VERSION 3.13)

set(PROJECT pico-weather-station)

if(DEVICE STREQUAL "pico")
    set(PICO_BOARD pico CACHE STRING "Board type" FORCE)
elseif(DEVICE STREQUAL "pico2")
    set(PICO_BOARD pico2 CACHE STRING "Board type" FORCE)
else()
    message(FATAL_ERROR
        "Unknown DEVICE='${DEVICE}'. Use pico, pico2 or seeed_xiao_rp2040."
    )
endif()

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(${PROJECT} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)

pico_sdk_init()

message(STATUS "DEVICE=${DEVICE}")
message(STATUS "PICO_BOARD=${PICO_BOARD}")
message(STATUS "PICO_PLATFORM=${PICO_PLATFORM}")

set(FREERTOS_CONFIG_FILE_DIRECTORY
    ${CMAKE_CURRENT_LIST_DIR}/include
)

set(FREERTOS_KERNEL_PATH
    ${CMAKE_CURRENT_LIST_DIR}/lib/FreeRTOS-Kernel
)

include(
    ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake
)

add_executable(${PROJECT}
    src/pico-weather-station.cpp
)

target_include_directories(${PROJECT} PRIVATE
    include/
    src/WindSpeedMonitor
    src/WindDirectionMonitor
)

add_subdirectory(
    lib/FreeRTOS-FAT-CLI-for-RPi-Pico/src/FreeRTOS+FAT+CLI
    freertos-fat
)

add_subdirectory(lib/pico-bme280)
add_subdirectory(lib/pico-ds3231)
add_subdirectory(lib/pico-uart-comms)
add_subdirectory(lib/pico-sdcard)
add_subdirectory(lib/pico-veml7700)

target_link_libraries(${PROJECT}
    pico_stdlib
    FreeRTOS-Kernel-Heap4
    pico_bme280
    pico_ds3231
    pico_uart_comms
    pico_sdcard
    pico_veml7700
)

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

pico_add_extra_outputs(${PROJECT})
