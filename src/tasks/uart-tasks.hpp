#pragma once

#include <stdio.h>

#include <cstring>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include "weather_data.hpp"

#include "UartComms.hpp"
#include "messages.hpp"

struct UARTTaskParams {
    UartComms* uart;
    WeatherData *weather_data;
    SemaphoreHandle_t weather_data_mutex;
    SemaphoreHandle_t uart_mutex;
    QueueHandle_t sdcard_queue;
    EventGroupHandle_t ready_events;
    uint32_t send_delay_ms;
};

void uart_send_task(void *pvParameters);
