#pragma once

#include <stdio.h>

#include <cstring>
#include <ctime>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include "weather_data.hpp"

#include "DS3231.h"

namespace ds3231_config {
    inline constexpr uint8_t ADDRESS = 0x68;
}

struct DS3231TaskParams {
    DS3231* sensor;
    WeatherData *weather_data;
    SemaphoreHandle_t i2c_mutex;
    SemaphoreHandle_t weather_data_mutex;
    uint32_t valid_sensor_bit;
    EventGroupHandle_t ready_events;
    EventBits_t ready_bit;
};

void ds3231_task(void *pvParameters);
void ds3231_setup_task(void *pvParameters);
