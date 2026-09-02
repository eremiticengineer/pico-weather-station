# Pico Weather Station

Solar powered pico weather station with LoRa broadcast of weather data.

work in progress...

## Cloning the project

Clone the project with FreeRTOS and sensor submodules to get the pico functionality:

```
git clone https://github.com/eremiticengineer/pico-weather-station
cd pico-weather-station
git submodule update --init --progress --jobs 4
git -C lib/FreeRTOS-Kernel submodule update --init --recursive --progress
git -C lib/fmt submodule update --init --recursive --progress
```

## FreeRTOS-Kernal setup for new projects

When creating a FreeRTOS project from scratch, clone the main branch into the project. The main branch at the moment has the necessary pico functionality:

```
git init
git submodule add https://github.com/FreeRTOS/FreeRTOS-Kernel.git lib/FreeRTOS-Kernel
git submodule add https://github.com/fmtlib/fmt lib/fmt
git submodule update --init --recursive --progress
git submodule add https://github.com/eremiticengineer/pico-bme280 lib/pico-bme280
git submodule add https://github.com/eremiticengineer/pico-ds3231 lib/pico-ds3231
git submodule add https://github.com/eremiticengineer/pico-uart-comms lib/pico-uart-comms
git add .gitmodules lib/FreeRTOS-Kernel
git add .gitmodules lib/fmt
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
