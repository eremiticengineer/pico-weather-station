#pragma once

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include "weather_data.hpp"

#include "WindSpeedMonitor.hpp"
#include "WindDirectionMonitor.hpp"

struct WindTaskParams {
    WindSpeedMonitor* wind_speed_monitor;
    WindDirectionMonitor* wind_direction_monitor;
    WeatherData *weather_data;
    SemaphoreHandle_t weather_data_mutex;
    uint32_t valid_sensor_bit_wind_speed;
    uint32_t valid_sensor_bit_wind_direction;
    EventGroupHandle_t ready_events;
    EventBits_t ready_bit_wind_speed;
    EventBits_t ready_bit_wind_direction;
};

void wind_speed_anemometer_pulse_task(void *pvParameters);
void wind_speed_task(void *pvParameters);
void wind_direction_task(void *pvParameters);

