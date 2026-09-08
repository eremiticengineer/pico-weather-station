#include "rain-tasks.hpp"

void rain_tipping_bucket_task(void *pvParameters) {
    RainTaskParams* pParam = static_cast<RainTaskParams*>(pvParameters);

    while (true) {
        // Notification from global ISR callback
        uint32_t pulses = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (xSemaphoreTake(pParam->weather_data_mutex, portMAX_DELAY)) {
            pParam->weather_data->rainTipsSinceBoot++;
            pParam->weather_data->validSensors |= pParam->valid_sensor_bit;
            xSemaphoreGive(pParam->weather_data_mutex);
        }
    }
}
