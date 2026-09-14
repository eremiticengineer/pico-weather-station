#include "wind-tasks.hpp"

void wind_speed_anemometer_pulse_task(void *pvParameters) {
    WindTaskParams* pParam = static_cast<WindTaskParams*>(pvParameters);

    while (true) {
        // Notification from global ISR callback
        uint32_t pulses = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        pParam->wind_speed_monitor->addPulses(pulses);
    }
}

void wind_speed_task(void* pvParameters) {
    WindTaskParams* pParam = static_cast<WindTaskParams*>(pvParameters);

    TickType_t last_wake = xTaskGetTickCount();

    // Treat startup as ready as otherwise the station will not boot on a calm, windless day
    xEventGroupSetBits(pParam->ready_events, pParam->ready_bit_wind_speed);

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));

        int32_t speed = pParam->wind_speed_monitor->sample1s();

        if (xSemaphoreTake(pParam->weather_data_mutex, portMAX_DELAY)) {
            pParam->weather_data->windSpeed = pParam->wind_speed_monitor->getRunningAverageMph() / 10.0f;
            pParam->weather_data->windGust = pParam->wind_speed_monitor->getCurrentMinuteMaxGustMph() / 10.0f;
            pParam->weather_data->validSensors |= pParam->valid_sensor_bit_wind_speed;
            xSemaphoreGive(pParam->weather_data_mutex);
        }

        printf(
            "Wind: %.1f mph, avg: %.1f mph, gust: %.1f mph\n",
            speed / 10.0,
            pParam->wind_speed_monitor->getRunningAverageMph() / 10.0,
            pParam->wind_speed_monitor->getCurrentMinuteMaxGustMph() / 10.0);
        }
}

void wind_direction_task(void* pvParameters) {
    WindTaskParams* pParam = static_cast<WindTaskParams*>(pvParameters);

    while (true) {
        auto wind_direction_data = pParam->wind_direction_monitor->getWindDirection();

        if (xSemaphoreTake(pParam->weather_data_mutex, portMAX_DELAY)) {
            snprintf(pParam->weather_data->windDirectionName,
                sizeof(pParam->weather_data->windDirectionName),
                "%s",
                wind_direction_data.name
            );
            pParam->weather_data->windDirectionDegrees = wind_direction_data.degrees;
            pParam->weather_data->validSensors |= pParam->valid_sensor_bit_wind_direction;

            xSemaphoreGive(pParam->weather_data_mutex);

            xEventGroupSetBits(pParam->ready_events, pParam->ready_bit_wind_direction);
            // Can set watchdog as wind direction is not interrupt driven
            xEventGroupSetBits(pParam->watchdog_events, pParam->watchdog_bit);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
