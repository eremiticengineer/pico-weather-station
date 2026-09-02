#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <string>
#include <fmt/base.h>

#include "BME280.h"
#include "DS3231.h"
#include "UartComms.hpp"
#include "sdcard.h"

#define DS3231_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
namespace ds3231_config {
    inline constexpr i2c_inst_t* I2C_INSTANCE = i2c0;
    inline constexpr uint8_t ADDRESS = 0x68;
    inline constexpr uint SDA = 8;
    inline constexpr uint SCL = 9;
}

#define BME280_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
namespace bme280_config {
    inline constexpr i2c_inst_t* I2C_INSTANCE = i2c0;
    inline constexpr uint8_t ADDRESS = 0x76;
    inline constexpr uint SDA = 8;
    inline constexpr uint SCL = 9;
}

#define UART_SEND_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
namespace uart_config {
    inline uart_inst_t* const UART_NUM = uart0;
    inline constexpr uint BAUD = 115200;
    inline constexpr uint TX = 0;
    inline constexpr uint RX = 1;
}

// All the i2c sensors share the semaphore
SemaphoreHandle_t i2c_mutex;
SemaphoreHandle_t uart_mutex;

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

void ds3231_task(void* pvParameters) {
    DS3231 *pDS3231 = static_cast<DS3231 *>(pvParameters);

    while (true) {
        struct tm time;
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
            if (pDS3231->readTime(time)) {
                printf("Date: %02d/%02d/%04d Time: %02d:%02d:%02d\n",
                time.tm_mday, time.tm_mon + 1, time.tm_year + 1900,
                time.tm_hour, time.tm_min, time.tm_sec);

                std::string dateTime;
                dateTime = fmt::format("{:02d}/{:02d}/{:04d} {:02d}:{:02d}:{:02d}",
                time.tm_mday, time.tm_mon + 1, time.tm_year + 1900,
                time.tm_hour, time.tm_min, time.tm_sec);

                printf("%s\n", dateTime.c_str());
            }

            xSemaphoreGive(i2c_mutex);
        }
        else {
            printf("Failed to read time\n");
        }

        vTaskDelay(1000);
    }
}

// void ds3231_setup_task(void* pvParameters) {
//     DS3231 *pDS3231 = static_cast<DS3231 *>(pvParameters);

//     struct tm buildTime = {
//         .tm_sec = 0,
//         .tm_min = 30,
//         .tm_hour = 14,
//         .tm_mday = 25,
//         .tm_mon = 12 - 1,        // Months since January, so January = 0
//         .tm_year = 2026 - 1900,  // Years since 1900
//         .tm_wday = 2,            // Optional: 0 = Sunday ... 6 = Saturday
//     };

//     if (pDS3231->setTime(buildTime)) {
//         printf("RTC time set to %04d-%02d-%02d %02d:%02d:%02d\n",
//                buildTime.tm_year + 1900, buildTime.tm_mon + 1, buildTime.tm_mday,
//                buildTime.tm_hour, buildTime.tm_min, buildTime.tm_sec);
//     }
//     else {
//         printf("Failed to set RTC time\n");
//     }

//     vTaskDelete(NULL); // Self-terminate
// }

void uart_send_task(void* params) {
    UartComms *pUartComms = static_cast<UartComms *>(params);

    std::string message = "comms data from pico";

    while (true) {
        if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(100))) {
            pUartComms->send(message);

            printf("wrote '%s', length=%zu\n", message.c_str(), message.size());

            xSemaphoreGive(uart_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main( void )
{
    stdio_init_all();

    sleep_ms(2000);

    // All the i2c sensors are on the same instance and same pins
    i2c_init(bme280_config::I2C_INSTANCE, 100 * 1000);
    gpio_set_function(bme280_config::SDA, GPIO_FUNC_I2C);
    gpio_set_function(bme280_config::SCL, GPIO_FUNC_I2C);
    gpio_pull_up(bme280_config::SDA);
    gpio_pull_up(bme280_config::SCL);

    i2c_mutex = xSemaphoreCreateMutex();

    BME280 bme280(bme280_config::I2C_INSTANCE, bme280_config::ADDRESS);
    xTaskCreate(bme280_task, "BME280Task", 512, (void*)&bme280, BME280_TASK_PRIORITY, nullptr);

    DS3231 ds3231(ds3231_config::I2C_INSTANCE, ds3231_config::ADDRESS);
    //xTaskCreate(ds3231_setup_task, "RTC Setup", 1024, (void*)&ds3231, tskIDLE_PRIORITY + 2, nullptr);
    xTaskCreate(ds3231_task, "DS3231 Task", 2048, (void*)&ds3231, DS3231_TASK_PRIORITY, nullptr);

    UartComms uartComms(
        uart_config::UART_NUM,
        uart_config::BAUD,
        uart_config::TX,
        uart_config::RX
    );
    uartComms.init();
    uart_mutex = xSemaphoreCreateMutex();
    xTaskCreate(uart_send_task, "UartSendTask", 512, (void*)&uartComms, UART_SEND_TASK_PRIORITY, nullptr);

    SDCard sdcard;
    sdcard.init();

    vTaskStartScheduler();

    return 0;
}
