#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "BME280.h"

/*
 * Each task is assigned a priority from 0 to ( configMAX_PRIORITIES - 1 ),
 * where configMAX_PRIORITIES is defined within FreeRTOSConfig.h.
 * Low priority numbers denote low priority tasks. The idle task has priority zero (tskIDLE_PRIORITY).
 * The task placed into the Running state is always the highest priority task that is able to run.
 */
#define BME280_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)

namespace bme280_config {
    inline constexpr i2c_inst_t* I2C_INSTANCE = i2c0;
    inline constexpr uint8_t ADDRESS = 0x76;
    inline constexpr uint SDA = 8;
    inline constexpr uint SCL = 9;
}

SemaphoreHandle_t i2c_mutex;

float temperature, pressure, humidity;

void bme280_task(void* pvParameters) {
    BME280 *pBME280 = static_cast<BME280 *>(pvParameters);

    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
        if (!pBME280->init()) {
            printf("Failed to init BME280\n");
            xSemaphoreGive(i2c_mutex);
            vTaskDelete(NULL);
        }
        xSemaphoreGive(i2c_mutex);
    }

    while (true) {
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
            if (pBME280->readSensor(temperature, pressure, humidity)) {
                printf("T: %.2f°C, P: %.2f hPa, H: %.2f%%\n", temperature, pressure, humidity);
            }
            xSemaphoreGive(i2c_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main( void )
{
    stdio_init_all();

    sleep_ms(2000);

    i2c_init(bme280_config::I2C_INSTANCE, 100 * 1000);
    gpio_set_function(bme280_config::SDA, GPIO_FUNC_I2C);
    gpio_set_function(bme280_config::SCL, GPIO_FUNC_I2C);
    gpio_pull_up(bme280_config::SDA);
    gpio_pull_up(bme280_config::SCL);

    i2c_mutex = xSemaphoreCreateMutex();

    BME280 bme280(bme280_config::I2C_INSTANCE, bme280_config::ADDRESS);
    xTaskCreate(bme280_task, "BME280Task", 512, (void*)&bme280, BME280_TASK_PRIORITY, nullptr);

    vTaskStartScheduler();

    return 0;
}
