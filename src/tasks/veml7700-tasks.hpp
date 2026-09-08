#pragma once

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include "weather_data.hpp"

#include "VEML7700.h"

namespace veml770_config {
    inline constexpr uint8_t ADDRESS = 0x10;
}

struct VEML7700TaskParams {
    VEML7700* sensor;
    WeatherData *weather_data;
    SemaphoreHandle_t i2c_mutex;
    SemaphoreHandle_t weather_data_mutex;
    uint32_t valid_sensor_bit;
    EventGroupHandle_t ready_events;
    EventBits_t ready_bit;
};

void veml7700_task(void *pvParameters);
