#pragma once

#include <stdio.h>
#include "hardware/irq.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include "weather_data.hpp"

struct RainTaskParams {
    WeatherData *weather_data;
    SemaphoreHandle_t weather_data_mutex;
    uint32_t valid_sensor_bit;
};

void wind_speed_and_rain_tipping_bucket_callback(uint gpio, __unused uint32_t events);
void rain_tipping_bucket_task(void *pvParameters);
