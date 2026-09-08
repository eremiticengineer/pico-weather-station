#include "veml7700-tasks.hpp"

void veml7700_task(void *pvParameters) {
    VEML7700TaskParams *pParams = static_cast<VEML7700TaskParams *>(pvParameters);

    if (xSemaphoreTake(pParams->i2c_mutex, portMAX_DELAY)) {
      if (!pParams->sensor->begin()) {
          xSemaphoreGive(pParams->i2c_mutex);
          printf("VEML7700 init failed\n");
          vTaskDelete(NULL);
      }
      xSemaphoreGive(pParams->i2c_mutex);
    }

    float luxValue;

    while (true) {
        if (xSemaphoreTake(pParams->i2c_mutex, portMAX_DELAY)) {
            bool sensor_read_success = pParams->sensor->readLux(luxValue);
            xSemaphoreGive(pParams->i2c_mutex);

            if (sensor_read_success) {
                if (xSemaphoreTake(pParams->weather_data_mutex, portMAX_DELAY)) {
                    pParams->weather_data->lux = luxValue;
                    pParams->weather_data->validSensors |= pParams->valid_sensor_bit;
                    xSemaphoreGive(pParams->weather_data_mutex);
                    xEventGroupSetBits(pParams->ready_events, pParams->ready_bit);
                }
            }
            else {
                pParams->weather_data->validSensors &= ~pParams->valid_sensor_bit;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
