#pragma once

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include "weather_data.hpp"

#include "sdcard.h"
#include "messages.hpp"

struct SDCardTaskParams {
    SDCard* sd_card;
    QueueHandle_t sdcard_queue;
};

void write_to_sdcard_task(void *pvParameters);
