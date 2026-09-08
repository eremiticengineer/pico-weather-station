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

#include "weather_data.hpp"

#include "WindSpeedMonitor.hpp"
#include "WindDirectionMonitor.hpp"

#include "tasks/bme280-tasks.hpp"

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

EventGroupHandle_t weather_ready_events;

/*
 * Set the weather_data.bootId so the base station knows how many
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

SemaphoreHandle_t weather_data_mutex;

TaskHandle_t rain_tipping_bucket_task_handle = nullptr;
TaskHandle_t wind_speed_monitor_task_handle = nullptr;

WeatherData weather_data;

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
            weather_data.rainTipsSinceBoot++;
            weather_data.validSensors |= SENSOR_VALID_RAIN_COUNT;
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
            weather_data.windSpeed = pWind_speed_monitor->getRunningAverageMph() / 10.0f;
            weather_data.windGust = pWind_speed_monitor->getCurrentMinuteMaxGustMph() / 10.0f;
            weather_data.validSensors |= SENSOR_VALID_WIND_SPEED;
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
            snprintf(weather_data.windDirectionName,
                sizeof(weather_data.windDirectionName),
                "%s",
                wind_direction_data.name
            );
            weather_data.windDirectionDegrees = wind_direction_data.degrees;
            weather_data.validSensors |= SENSOR_VALID_WIND_DIRECTION;

            xSemaphoreGive(weather_data_mutex);

            xEventGroupSetBits(weather_ready_events, WIND_DIRECTION_READY);
        }

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
        }
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
                    weather_data.timestamp = static_cast<uint32_t>(timestamp);

                    std::strncpy(weather_data.dateTime, dateTime, sizeof(weather_data.dateTime) - 1);

                    weather_data.validSensors |= SENSOR_VALID_DS3231;

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
                    weather_data.lux = luxValue;
                    weather_data.validSensors |= SENSOR_VALID_VEML7700;
                    xSemaphoreGive(weather_data_mutex);
                    xEventGroupSetBits(weather_ready_events, VEML7700_READY);
                }
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
        pdMS_TO_TICKS(5000)
    );    

    while (true) {
        WeatherData snapshot;
        std::string lora_message;
        std::string sdcard_message;

        if (xSemaphoreTake(weather_data_mutex, portMAX_DELAY)) {
            snapshot = weather_data;

            xSemaphoreGive(weather_data_mutex);

            // CSV for the LoRa broadcaster and sdcard
            char buffer[256];
            snprintf(
                buffer,
                sizeof(buffer),
                "%u,%u,%.1f,%.1f,%.1f,%.1f,%.1f,%s,%u,%.1f,%u,%.2f,%u",
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
                snapshot.batteryVoltage,
                static_cast<unsigned>(snapshot.validSensors)
            );
            lora_message = buffer;
        }

        if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(100))) {
            pUartComms->send(lora_message);

            printf("sent to uart '%s', length=%zu\n", lora_message.c_str(), lora_message.size());

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

    weather_data.bootId = create_boot_id();

    SDCard sd_card;

    // All the i2c sensors are on the same instance and same pins
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(8, GPIO_FUNC_I2C);
    gpio_set_function(9, GPIO_FUNC_I2C);
    gpio_pull_up(8);
    gpio_pull_up(9);

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
    
    // Make uart_send_task wait for the sensors to take their first reading
    weather_ready_events = xEventGroupCreate();
 
    weather_data_mutex = xSemaphoreCreateMutex();
    i2c_mutex = xSemaphoreCreateMutex();
    uart_mutex = xSemaphoreCreateMutex();

    BME280 bme280(i2c0, bme280_config::ADDRESS);
    BME280TaskParams bme280_task_params {
        .sensor = &bme280,
        .weather_data = &weather_data,
        .i2c_mutex = i2c_mutex,
        .weather_data_mutex = weather_data_mutex,
        .valid_sensor_bit = SENSOR_VALID_BME280,
        .ready_events = weather_ready_events,
        .ready_bit = BME280_READY
    };
    constexpr UBaseType_t BME280_TASK_PRIORITY = tskIDLE_PRIORITY + 2UL;
    constexpr configSTACK_DEPTH_TYPE BME280_TASK_STACK_SIZE = 512;


    DS3231 ds3231(ds3231_config::I2C_INSTANCE, ds3231_config::ADDRESS);

    VEML7700 veml770(veml770_config::I2C_INSTANCE, veml770_config::ADDRESS);

    UartComms uartComms(
        uart_config::UART_NUM,
        uart_config::BAUD,
        uart_config::TX,
        uart_config::RX
    );
    uartComms.init();










    sdcard_queue = xQueueCreate(8, sizeof(SDCardMessage));

    //xTaskCreate(ds3231_setup_task, "RTC Setup", 1024, (void*)&ds3231, tskIDLE_PRIORITY + 2, nullptr);
    xTaskCreate(ds3231_task, "DS3231 Task", 2048, (void*)&ds3231, DS3231_TASK_PRIORITY, nullptr);

    xTaskCreate(bme280_task, "BME280Task", BME280_TASK_STACK_SIZE, (void*)&bme280_task_params, BME280_TASK_PRIORITY, nullptr);

    xTaskCreate(veml7700_task, "VEML7700Task", 512, (void*)&veml770, VEML7700_SEND_TASK_PRIORITY, nullptr);
    xTaskCreate(uart_send_task, "UartSendTask", 2048, (void*)&uartComms, UART_SEND_TASK_PRIORITY, nullptr);
    xTaskCreate(rain_tipping_bucket_task, "RainTippingBucketTask", 512, nullptr, RAIN_TASK_PRIORITY, &rain_tipping_bucket_task_handle);
    xTaskCreate(wind_speed_monitor_task, "WindSpeedMonitorTask", 512, (void*)&wind_speed_monitor, WIND_SPEED_MONITOR_TASK_PRIORITY, &wind_speed_monitor_task_handle);
    xTaskCreate(wind_direction_monitor_task, "WindDirectionMonitorTask", 512, (void*)&wind_direction_monitor, WIND_DIRECTION_MONITOR_TASK_PRIORITY, nullptr);
    xTaskCreate(write_to_sdcard_task, "WriteToSDCardTask", 4096, (void*)&sd_card, SDCARD_TASK_PRIORITY, nullptr);

    vTaskStartScheduler();

    return 0;
}
