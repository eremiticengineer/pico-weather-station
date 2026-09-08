#pragma once

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include "weather_data.hpp"

#include "BME280.h"

namespace bme280_config {
    //inline constexpr uint8_t ADDRESS = 0x76;
    // https://www.waveshare.com/wiki/BME280_Environmental_Sensor
    // Address chip select (default is high):
    // When the voltage is high, the address is 0 x 77
    // When the voltage is low, the address is: 0 x 76
    inline constexpr uint8_t ADDRESS = 0x77;
}

struct BME280TaskParams {
    BME280* sensor;
    WeatherData *weather_data;
    SemaphoreHandle_t i2c_mutex;
    SemaphoreHandle_t weather_data_mutex;
    uint32_t valid_sensor_bit;
    EventGroupHandle_t ready_events;
    EventBits_t ready_bit;
};

void bme280_task(void *pvParameters);
