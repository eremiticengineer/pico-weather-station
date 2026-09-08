#include "ds3231-tasks.hpp"

void ds3231_task(void* pvParameters) {
    DS3231TaskParams* pParams = static_cast<DS3231TaskParams *>(pvParameters);

    struct tm time;

    while (true) {
        if (xSemaphoreTake(pParams->i2c_mutex, portMAX_DELAY)) {
            bool sensor_read_success = pParams->sensor->readTime(time);

            xSemaphoreGive(pParams->i2c_mutex);

            if (sensor_read_success) {
                time_t timestamp = mktime(&time);

                char dateTime[20];

                snprintf(dateTime,
                    sizeof(dateTime),
                    "%02d/%02d/%04d %02d:%02d:%02d",
                    time.tm_mday,
                    time.tm_mon + 1,
                    time.tm_year + 1900,
                    time.tm_hour,
                    time.tm_min,
                    time.tm_sec
                );

                if (xSemaphoreTake(pParams->weather_data_mutex, portMAX_DELAY)) {
                    pParams->weather_data->timestamp = static_cast<uint32_t>(timestamp);

                    std::strncpy(pParams->weather_data->dateTime, dateTime, sizeof(pParams->weather_data->dateTime) - 1);

                    pParams->weather_data->validSensors |= pParams->valid_sensor_bit;

                    xSemaphoreGive(pParams->weather_data_mutex);

                    xEventGroupSetBits(pParams->ready_events, pParams->ready_bit);
                }

                // printf("Date: %02d/%02d/%04d Time: %02d:%02d:%02d timestamp=%lu\n",
                //     time.tm_mday,
                //     time.tm_mon + 1,
                //     time.tm_year + 1900,
                //     time.tm_hour,
                //     time.tm_min,
                //     time.tm_sec,
                //     static_cast<unsigned long>(timestamp)
                // );
            }
        }
        else {
            pParams->weather_data->validSensors &= ~pParams->valid_sensor_bit;
        }

        vTaskDelay(1000);
    }
}

void ds3231_setup_task(void* pvParameters) {
    DS3231 *pDS3231 = static_cast<DS3231 *>(pvParameters);

    struct tm buildTime = {
        .tm_sec = 0,
        .tm_min = 30,
        .tm_hour = 14,
        .tm_mday = 25,
        .tm_mon = 12 - 1,        // Months since January, so January = 0
        .tm_year = 2026 - 1900,  // Years since 1900
        .tm_wday = 2,            // Optional: 0 = Sunday ... 6 = Saturday
    };

    if (pDS3231->setTime(buildTime)) {
        printf("RTC time set to %04d-%02d-%02d %02d:%02d:%02d\n",
               buildTime.tm_year + 1900, buildTime.tm_mon + 1, buildTime.tm_mday,
               buildTime.tm_hour, buildTime.tm_min, buildTime.tm_sec);
    }
    else {
        printf("Failed to set RTC time\n");
    }

    vTaskDelete(NULL); // Self-terminate
}
