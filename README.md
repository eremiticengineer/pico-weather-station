# Pico Weather Station

Solar powered pico weather station with LoRa broadcast of weather data.

There are three modules in the system:

The main weather station that orchestrates sensors and sends the data over UART to the LoRa broadcaster.

[pico-weather-station](https://github.com/eremiticengineer/pico-weather-station)

The LoRa broadcaster which receives data over UART from the main station and sends it over LoRa 433Mhz.

[pico-weather-station-lora-broadcaster](https://github.com/eremiticengineer/pico-weather-station-lora-broadcaster)

The base station that receives LoRa messages from the station via the LoRa broadcaster and processes the sensor data.

Pico weather station base station, a work in progress...

## Cloning and building the project

Clone the project with FreeRTOS and sensor submodules to get the pico functionality:

```
git clone https://github.com/eremiticengineer/pico-weather-station
cd pico-weather-station
git submodule update --init --progress --jobs 4
git -C lib/FreeRTOS-Kernel submodule update --init --recursive --progress
git -C lib/FreeRTOS-FAT-CLI-for-RPi-Pico submodule update --init --recursive --progress
./build_project pico|pico2
```

## FreeRTOSConfig.h

This file customises FreeRTOS for your project. The file:

```
include/FreeRTOSConfig.h
```

is this one from the pico-examples:

```
pico-examples/freertos/FreeRTOSConfig_examples_common.h
```

## References

* [Task priorites](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/01-Tasks-and-co-routines/03-Task-priorities)
* [uxTaskGetStackHighWaterMark](https://www.freertos.org/Documentation/02-Kernel/04-API-references/03-Task-utilities/04-uxTaskGetStackHighWaterMark)
