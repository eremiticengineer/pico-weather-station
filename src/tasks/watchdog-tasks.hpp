#pragma once

#include <stdio.h>
#include "hardware/watchdog.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include "weather_data.hpp"

struct WatchdogTaskParams {
    EventGroupHandle_t watchdog_events;
};

void watchdog_task(void *pvParameters);