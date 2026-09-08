#include "bme280-tasks.hpp"

void bme280_task(void* pvParameters) {
    BME280TaskParams *pParams = static_cast<BME280TaskParams *>(pvParameters);

    if (xSemaphoreTake(pParams->i2c_mutex, portMAX_DELAY)) {
        if (!pParams->sensor->init()) {
            printf("Failed to init BME280\n");
            xSemaphoreGive(pParams->i2c_mutex);
            vTaskDelete(NULL);
        }
        xSemaphoreGive(pParams->i2c_mutex);
    }

    float temperature;
    float pressure;
    float humidity;

    while (true) {
        if (xSemaphoreTake(pParams->i2c_mutex, portMAX_DELAY)) {
            bool sensor_read_success = pParams->sensor->readSensor(temperature, pressure, humidity);

            xSemaphoreGive(pParams->i2c_mutex);

            if (sensor_read_success) {
                if (xSemaphoreTake(pParams->weather_data_mutex, portMAX_DELAY)) {
                    pParams->weather_data->temperature = temperature;
                    pParams->weather_data->pressure = pressure;
                    pParams->weather_data->humidity = humidity;
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
