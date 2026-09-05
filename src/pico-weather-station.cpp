#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/structs/rosc.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include <string>
#include <ctime>
#include <cstring>

#include "WindSpeedMonitor.hpp"
#include "WindDirectionMonitor.hpp"

#include "BME280.h"
#include "DS3231.h"
#include "UartComms.hpp"
#include "sdcard.h"
#include "VEML7700.h"

/*
 * Send to the LoRa broadcaster every 10s.
 * That will keep the duty cycle well below the 10% limit set by OFCOM in the UK.
 */
#define UART_SEND_DELAY_MS 10000

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

#define VEML7700_SEND_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
namespace veml770_config {
    inline constexpr i2c_inst_t* I2C_INSTANCE = i2c0;
    inline constexpr uint8_t ADDRESS = 0x10;
    inline constexpr uint SDA = 8;
    inline constexpr uint SCL = 9;
}

#define UART_SEND_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
namespace uart_config {
    inline uart_inst_t* const UART_NUM = uart1;
    inline constexpr uint BAUD = 115200;
    inline constexpr uint TX = 4;
    inline constexpr uint RX = 5;
}

#define RAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
namespace rain_config {
    inline constexpr uint INTERRUPT_PIN = 14;
    inline constexpr bool CALLBACK_ENABLED = true;
}

#define WIND_SPEED_MONITOR_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
namespace wind_speed_config {
    inline constexpr uint INTERRUPT_PIN = 15;
    inline constexpr bool CALLBACK_ENABLED = true;
}

#define WIND_DIRECTION_MONITOR_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
namespace wind_direction_config {
    inline spi_inst_t* SPI_INSTANCE = spi0;
    inline constexpr uint CS_PIN = 17;
    inline constexpr uint CLK_PIN = 18;
    inline constexpr uint MOSI_PIN = 19;
    inline constexpr uint MISO_PIN = 16;
}

#define SDCARD_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)

// All the i2c sensors share the semaphore
SemaphoreHandle_t i2c_mutex;
SemaphoreHandle_t uart_mutex;

struct SDCardMessage {
    char data[256];
};

QueueHandle_t sdcard_queue;

static WindSpeedMonitor wind_speed_monitor;

// Startup for sensors to report readiness
EventGroupHandle_t weather_ready_events;
constexpr EventBits_t BME280_READY          = 1 << 0;
constexpr EventBits_t VEML7700_READY        = 1 << 1;
constexpr EventBits_t WIND_DIRECTION_READY  = 1 << 2;
constexpr EventBits_t WIND_SPEED_READY      = 1 << 3;
constexpr EventBits_t DS3231_READY          = 1 << 4;
constexpr EventBits_t ALL_READY =
    BME280_READY |
    VEML7700_READY |
    WIND_DIRECTION_READY |
    WIND_SPEED_READY |
    DS3231_READY;

/*
 * Set the WeatherData.bootId so the base station knows how many
 * rainTipsSinceBoot there are as this will reset to zero after
 * a reboot.
 */
uint32_t create_boot_id() {
    uint32_t value = time_us_32();

    value ^= rosc_hw->randombit << 0;
    value ^= rosc_hw->randombit << 7;
    value ^= rosc_hw->randombit << 13;
    value ^= rosc_hw->randombit << 21;
    value ^= rosc_hw->randombit << 29;

    value ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&value));

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;

    return value;
}

struct WeatherData {
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;

    float windSpeed = 0.0f;
    float windGust = 0.0f;
    char windDirectionName[4];
    uint16_t windDirectionDegrees = 0;

    float lux = 0.0f;

    uint32_t rainTipsSinceBoot = 0;
    uint32_t bootId = create_boot_id();

    float batteryVoltage = 0.0f;

    uint32_t timestamp = 0;

    char dateTime[20] = "";
}; WeatherData weatherData;

SemaphoreHandle_t weather_data_mutex;

TaskHandle_t rain_tipping_bucket_task_handle = nullptr;
TaskHandle_t wind_speed_monitor_task_handle = nullptr;

void wind_speed_and_rain_tipping_bucket_callback(uint gpio, __unused uint32_t events) {
  if (gpio == rain_config::INTERRUPT_PIN) {
      BaseType_t higher_priority_task_woken = pdFALSE;
      vTaskNotifyGiveFromISR(rain_tipping_bucket_task_handle, &higher_priority_task_woken);
      portYIELD_FROM_ISR(higher_priority_task_woken);
  }
  else if (gpio == wind_speed_config::INTERRUPT_PIN) {
    wind_speed_monitor.onPulse();
  }
}

void rain_tipping_bucket_task(void *pvParameters) {
    while (true) {
        uint32_t pulses = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (xSemaphoreTake(weather_data_mutex, portMAX_DELAY)) {
            weatherData.rainTipsSinceBoot++;
            xSemaphoreGive(weather_data_mutex);
        }
    }
}

void wind_speed_monitor_task(void* parameter) {
    auto* pWind_speed_monitor = static_cast<WindSpeedMonitor*>(parameter);

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));

        int32_t speed = pWind_speed_monitor->sample1s();

        if (xSemaphoreTake(weather_data_mutex, portMAX_DELAY)) {
            weatherData.windSpeed = pWind_speed_monitor->getRunningAverageMph() / 10.0f;
            weatherData.windGust = pWind_speed_monitor->getCurrentMinuteMaxGustMph() / 10.0f;
            xSemaphoreGive(weather_data_mutex);
            xEventGroupSetBits(weather_ready_events, WIND_SPEED_READY);
        }

        printf(
            "Wind: %.1f mph, avg: %.1f mph, gust: %.1f mph\n",
            speed / 10.0,
            pWind_speed_monitor->getRunningAverageMph() / 10.0,
            pWind_speed_monitor->getCurrentMinuteMaxGustMph() / 10.0);
        }
}

void wind_direction_monitor_task(void* parameter) {
    auto* pWind_direction_monitor = static_cast<WindDirectionMonitor*>(parameter);

    while (true) {
        auto wind_direction_data = pWind_direction_monitor->getWindDirection();

        if (xSemaphoreTake(weather_data_mutex, portMAX_DELAY)) {
            snprintf(weatherData.windDirectionName,
                sizeof(weatherData.windDirectionName),
                "%s",
                wind_direction_data.name
            );
            weatherData.windDirectionDegrees = wind_direction_data.degrees;

            xSemaphoreGive(weather_data_mutex);

            xEventGroupSetBits(weather_ready_events, WIND_DIRECTION_READY);
        }

        // printf("Wind direction name = %s\n", wind_direction_data.name);
        // printf("Wind direction degrees = %.1f\n", wind_direction_data.degrees);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void write_to_sdcard_task(void* pvParameters) {
    SDCard *pSDCard = static_cast<SDCard *>(pvParameters);
    pSDCard->init();

    SDCardMessage message;

    while (true)
    {
        if (xQueueReceive(sdcard_queue, &message, portMAX_DELAY) == pdTRUE) {
            pSDCard->writeAfterInit(message.data);

            printf("written to sdcard: '%s'\n", message.data);
        }
    }
}

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

    float temperature;
    float pressure;
    float humidity;

    while (true) {
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
            bool sensor_read_success = pBME280->readSensor(temperature, pressure, humidity);

            xSemaphoreGive(i2c_mutex);

            if (sensor_read_success) {
                if (xSemaphoreTake(weather_data_mutex, portMAX_DELAY)) {
                    weatherData.temperature = temperature;
                    weatherData.pressure = pressure;
                    weatherData.humidity = humidity;

                    xSemaphoreGive(weather_data_mutex);

                    xEventGroupSetBits(weather_ready_events, BME280_READY);
                }

                // printf("T: %.2f°C, P: %.2f hPa, H: %.2f%%\n",
                //     temperature,
                //     pressure,
                //     humidity
                // );
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void ds3231_task(void* pvParameters) {
    DS3231 *pDS3231 = static_cast<DS3231 *>(pvParameters);

    struct tm time;

    while (true) {
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
            bool sensor_read_success = pDS3231->readTime(time);

            xSemaphoreGive(i2c_mutex);

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

                if (xSemaphoreTake(weather_data_mutex, portMAX_DELAY)) {
                    weatherData.timestamp = static_cast<uint32_t>(timestamp);

                    std::strncpy(weatherData.dateTime, dateTime, sizeof(weatherData.dateTime) - 1);

                    xSemaphoreGive(weather_data_mutex);

                    xEventGroupSetBits(weather_ready_events, DS3231_READY);
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

void veml7700_task(void *pvParameters) {
    VEML7700 *pVEML7700 = static_cast<VEML7700 *>(pvParameters);

    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
      if (!pVEML7700->begin()) {
          xSemaphoreGive(i2c_mutex);
          printf("VEML7700 init failed\n");
          vTaskDelete(NULL);
      }
      xSemaphoreGive(i2c_mutex);
    }

    float luxValue;

    while (true) {
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
            bool sensor_read_success = pVEML7700->readLux(luxValue);
            if (sensor_read_success) {
                if (xSemaphoreTake(weather_data_mutex, portMAX_DELAY)) {
                    weatherData.lux = luxValue;
                    xSemaphoreGive(weather_data_mutex);
                    xEventGroupSetBits(weather_ready_events, VEML7700_READY);
                }

                // printf("Lux: %.2f\n", luxValue);
            }
            else {
                printf("Failed to read lux\n");
            }
            xSemaphoreGive(i2c_mutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1s delay
    }
}

void uart_send_task(void* params) {
    UartComms *pUartComms = static_cast<UartComms *>(params);

    // Wait for the sensors to take their first reading
    xEventGroupWaitBits(
        weather_ready_events,
        ALL_READY,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );    

    while (true) {
        WeatherData snapshot;
        std::string lora_message;
        std::string sdcard_message;

        if (xSemaphoreTake(weather_data_mutex, portMAX_DELAY)) {
            snapshot = weatherData;

            xSemaphoreGive(weather_data_mutex);

            // CSV for the LoRa broadcaster and sdcard
            char buffer[256];
            snprintf(
                buffer,
                sizeof(buffer),
                "%u,%u,%.1f,%.1f,%.1f,%.1f,%.1f,%s,%u,%.1f,%u,%.2f",
                static_cast<unsigned>(snapshot.timestamp),
                snapshot.bootId,
                snapshot.temperature,
                snapshot.pressure,
                snapshot.humidity,
                snapshot.windSpeed,
                snapshot.windGust,
                snapshot.windDirectionName,
                static_cast<unsigned>(snapshot.windDirectionDegrees),
                snapshot.lux,
                static_cast<unsigned>(snapshot.rainTipsSinceBoot),
                snapshot.batteryVoltage
            );
            lora_message = buffer;
        }

        if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(100))) {
            pUartComms->send(lora_message);

            printf("wrote '%s', length=%zu\n", lora_message.c_str(), lora_message.size());

            xSemaphoreGive(uart_mutex);
        }

        SDCardMessage sdcard_message_to_send {};

        std::strncpy(sdcard_message_to_send.data, lora_message.c_str(), sizeof(sdcard_message_to_send.data) - 1);

        xQueueSend(sdcard_queue, &sdcard_message_to_send, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(UART_SEND_DELAY_MS));
    }
}

int main( void )
{
    stdio_init_all();

    sleep_ms(2000);

    SDCard sdcard;

    weather_data_mutex = xSemaphoreCreateMutex();

    // All the i2c sensors are on the same instance and same pins
    i2c_init(bme280_config::I2C_INSTANCE, 100 * 1000);
    gpio_set_function(bme280_config::SDA, GPIO_FUNC_I2C);
    gpio_set_function(bme280_config::SCL, GPIO_FUNC_I2C);
    gpio_pull_up(bme280_config::SDA);
    gpio_pull_up(bme280_config::SCL);
    i2c_mutex = xSemaphoreCreateMutex();

    // Rain tipping bucket interrupt
    gpio_init(rain_config::INTERRUPT_PIN);
    gpio_set_dir(rain_config::INTERRUPT_PIN, GPIO_IN);
    gpio_pull_down(rain_config::INTERRUPT_PIN);
    gpio_set_irq_enabled_with_callback(rain_config::INTERRUPT_PIN, GPIO_IRQ_EDGE_RISE,
        rain_config::CALLBACK_ENABLED, wind_speed_and_rain_tipping_bucket_callback);

    // Wind speed interrupt
    gpio_init(wind_speed_config::INTERRUPT_PIN);
    gpio_set_dir(wind_speed_config::INTERRUPT_PIN, GPIO_IN);
    gpio_pull_down(wind_speed_config::INTERRUPT_PIN);
    gpio_set_irq_enabled_with_callback(wind_speed_config::INTERRUPT_PIN, GPIO_IRQ_EDGE_RISE,
        wind_speed_config::CALLBACK_ENABLED, wind_speed_and_rain_tipping_bucket_callback);

    WindDirectionMonitor wind_direction_monitor(
        wind_direction_config::SPI_INSTANCE,
        wind_direction_config::CS_PIN,
        wind_direction_config::CLK_PIN,
        wind_direction_config::MOSI_PIN,
        wind_direction_config::MISO_PIN
    );
    wind_direction_monitor.init();
    
    BME280 bme280(bme280_config::I2C_INSTANCE, bme280_config::ADDRESS);

    DS3231 ds3231(ds3231_config::I2C_INSTANCE, ds3231_config::ADDRESS);

    VEML7700 veml770(veml770_config::I2C_INSTANCE, veml770_config::ADDRESS);

    UartComms uartComms(
        uart_config::UART_NUM,
        uart_config::BAUD,
        uart_config::TX,
        uart_config::RX
    );
    uartComms.init();
    uart_mutex = xSemaphoreCreateMutex();

    sdcard_queue = xQueueCreate(8, sizeof(SDCardMessage));

    // Make uart_send_task wait for the sensors to take their first reading
    weather_ready_events = xEventGroupCreate();

    //xTaskCreate(ds3231_setup_task, "RTC Setup", 1024, (void*)&ds3231, tskIDLE_PRIORITY + 2, nullptr);
    xTaskCreate(ds3231_task, "DS3231 Task", 2048, (void*)&ds3231, DS3231_TASK_PRIORITY, nullptr);
    xTaskCreate(bme280_task, "BME280Task", 512, (void*)&bme280, BME280_TASK_PRIORITY, nullptr);
    xTaskCreate(veml7700_task, "VEML7700Task", 512, (void*)&veml770, VEML7700_SEND_TASK_PRIORITY, nullptr);
    xTaskCreate(uart_send_task, "UartSendTask", 2048, (void*)&uartComms, UART_SEND_TASK_PRIORITY, nullptr);
    xTaskCreate(rain_tipping_bucket_task, "RainTippingBucketTask", 512, nullptr, RAIN_TASK_PRIORITY, &rain_tipping_bucket_task_handle);
    xTaskCreate(wind_speed_monitor_task, "WindSpeedMonitorTask", 512, (void*)&wind_speed_monitor, WIND_SPEED_MONITOR_TASK_PRIORITY, &wind_speed_monitor_task_handle);
    xTaskCreate(wind_direction_monitor_task, "WindDirectionMonitorTask", 512, (void*)&wind_direction_monitor, WIND_DIRECTION_MONITOR_TASK_PRIORITY, nullptr);
    xTaskCreate(write_to_sdcard_task, "WriteToSDCardTask", 4096, (void*)&sdcard, SDCARD_TASK_PRIORITY, nullptr);

    vTaskStartScheduler();

    return 0;
}
