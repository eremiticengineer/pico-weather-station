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
#include "tasks/veml7700-tasks.hpp"
#include "tasks/ds3231-tasks.hpp"
#include "tasks/uart-tasks.hpp"
#include "tasks/sdcard-tasks.hpp"
#include "tasks/wind-tasks.hpp"

/*
 * Send to the LoRa broadcaster every 10s.
 * That will keep the duty cycle well below the 10% limit set by OFCOM in the UK.
 */
#define UART_SEND_DELAY_MS 10000

namespace wind_speed_config {
    inline constexpr uint INTERRUPT_PIN = 15;
    inline constexpr bool CALLBACK_ENABLED = true;
}
WindSpeedMonitor wind_speed_monitor;

namespace wind_direction_config {
    inline spi_inst_t* SPI_INSTANCE = spi0;
    inline constexpr uint CS_PIN = 17;
    inline constexpr uint CLK_PIN = 18;
    inline constexpr uint MOSI_PIN = 19;
    inline constexpr uint MISO_PIN = 16;
}
WindDirectionMonitor wind_direction_monitor (
    wind_direction_config::SPI_INSTANCE,
    wind_direction_config::CS_PIN,
    wind_direction_config::CLK_PIN,
    wind_direction_config::MOSI_PIN,
    wind_direction_config::MISO_PIN
);




#define RAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
namespace rain_config {
    inline constexpr uint INTERRUPT_PIN = 14;
    inline constexpr bool CALLBACK_ENABLED = true;
}





// All the i2c sensors share the semaphore
SemaphoreHandle_t i2c_mutex;
SemaphoreHandle_t uart_mutex;


QueueHandle_t sdcard_queue;



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




int main( void )
{
    stdio_init_all();

    sleep_ms(2000);

    weather_data.bootId = create_boot_id();

    

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

    





    // Make uart_send_task wait for the sensors to take their first reading
    weather_ready_events = xEventGroupCreate();
 
    weather_data_mutex = xSemaphoreCreateMutex();
    i2c_mutex = xSemaphoreCreateMutex();
    
    BME280 bme280(i2c0, bme280_config::ADDRESS);
    BME280TaskParams bme280_task_params {
        // The sensor for the task
        .sensor = &bme280,
        // Where the task will store the data from the sensor
        .weather_data = &weather_data,
        // To allow the task to take control of the i2c bus
        .i2c_mutex = i2c_mutex,
        // To allow the task to take control of the struct
        // that stores the global weather data across sensors
        .weather_data_mutex = weather_data_mutex,
        // The sensor specific bit to set or clear
        // depending on whether the sensor is working or not
        .valid_sensor_bit = SENSOR_VALID_BME280,
        // The startup bit field that lets the uart comms wait
        // for all sensors to take their first reading
        .ready_events = weather_ready_events,
        // The sensor specific bit in the startup bit field
        .ready_bit = BME280_READY
    };
    constexpr UBaseType_t BME280_TASK_PRIORITY = tskIDLE_PRIORITY + 2UL;
    constexpr configSTACK_DEPTH_TYPE BME280_TASK_STACK_SIZE = 512;

    VEML7700 veml770(i2c0, veml770_config::ADDRESS);
    VEML7700TaskParams veml7700_task_params {
        .sensor = &veml770,
        .weather_data = &weather_data,
        .i2c_mutex = i2c_mutex,
        .weather_data_mutex = weather_data_mutex,
        .valid_sensor_bit = SENSOR_VALID_VEML7700,
        .ready_events = weather_ready_events,
        .ready_bit = VEML7700_READY
    };
    constexpr UBaseType_t VEML7700_TASK_PRIORITY = tskIDLE_PRIORITY + 2UL;
    constexpr configSTACK_DEPTH_TYPE VEML7700_TASK_STACK_SIZE = 512;

    DS3231 ds3231(i2c0, ds3231_config::ADDRESS);
    DS3231TaskParams ds3231_task_params {
        .sensor = &ds3231,
        .weather_data = &weather_data,
        .i2c_mutex = i2c_mutex,
        .weather_data_mutex = weather_data_mutex,
        .valid_sensor_bit = SENSOR_VALID_DS3231,
        .ready_events = weather_ready_events,
        .ready_bit = DS3231_READY
    };
    constexpr UBaseType_t DS3231_TASK_PRIORITY = tskIDLE_PRIORITY + 2UL;
    constexpr configSTACK_DEPTH_TYPE DS3231_TASK_STACK_SIZE = 2048;

    uint uart_tx_pin = 4;
    uint uart_rx_pin = 5;
    UartComms uartComms(uart1, 115200, uart_tx_pin, uart_rx_pin);
    uartComms.init();
    uart_mutex = xSemaphoreCreateMutex();
    sdcard_queue = xQueueCreate(8, sizeof(SDCardMessage));
    UARTTaskParams uart_task_params {
        .uart = &uartComms,
        .weather_data = &weather_data,
        .weather_data_mutex = weather_data_mutex,
        .uart_mutex = uart_mutex,
        .sdcard_queue = sdcard_queue,
        .ready_events = weather_ready_events,
        .send_delay_ms = UART_SEND_DELAY_MS
    };
    constexpr UBaseType_t UART_SEND_TASK_PRIORITY = tskIDLE_PRIORITY + 2UL;
    constexpr configSTACK_DEPTH_TYPE UART_SEND_TASK_STACK_SIZE = 2048;

    SDCard sd_card;
    SDCardTaskParams sdcard_task_params {
        .sd_card = &sd_card,
        .sdcard_queue = sdcard_queue
    };
    constexpr UBaseType_t WRITE_TO_SDCARD_TASK_PRIORITY = tskIDLE_PRIORITY + 2UL;
    constexpr configSTACK_DEPTH_TYPE WRITE_TO_SDCARD_TASK_STACK_SIZE = 4096;

    wind_direction_monitor.init();
    WindTaskParams wind_task_params {
        .wind_speed_monitor = &wind_speed_monitor,
        .wind_direction_monitor = &wind_direction_monitor,
        .weather_data = &weather_data,
        .weather_data_mutex = weather_data_mutex,
        .valid_sensor_bit_wind_speed = SENSOR_VALID_WIND_DIRECTION,
        .valid_sensor_bit_wind_direction = SENSOR_VALID_WIND_DIRECTION,
        .ready_events = weather_ready_events,
        .ready_bit_wind_speed = WIND_SPEED_READY,
        .ready_bit_wind_direction = WIND_DIRECTION_READY
    };
    constexpr UBaseType_t WIND_SPEED_TASK_PRIORITY = tskIDLE_PRIORITY + 2UL;
    constexpr configSTACK_DEPTH_TYPE WIND_SPEED_TASK_STACK_SIZE = 512;
    constexpr UBaseType_t WIND_DIRECTION_TASK_PRIORITY = tskIDLE_PRIORITY + 2UL;
    constexpr configSTACK_DEPTH_TYPE WIND_DIRECTION_TASK_STACK_SIZE = 512;



    //xTaskCreate(ds3231_setup_task, "RTC Setup", 1024, (void*)&ds3231, tskIDLE_PRIORITY + 2, nullptr);
    xTaskCreate(ds3231_task, "DS3231 Task", DS3231_TASK_STACK_SIZE, (void*)&ds3231_task_params, DS3231_TASK_PRIORITY, nullptr);

    xTaskCreate(bme280_task, "BME280Task", BME280_TASK_STACK_SIZE, (void*)&bme280_task_params, BME280_TASK_PRIORITY, nullptr);

    xTaskCreate(veml7700_task, "VEML7700Task", VEML7700_TASK_STACK_SIZE, (void*)&veml7700_task_params, VEML7700_TASK_PRIORITY, nullptr);

    xTaskCreate(uart_send_task, "UartSendTask", UART_SEND_TASK_STACK_SIZE, (void*)&uart_task_params, UART_SEND_TASK_PRIORITY, nullptr);

    xTaskCreate(write_to_sdcard_task, "WriteToSDCardTask", WRITE_TO_SDCARD_TASK_STACK_SIZE, (void*)&sdcard_task_params, WRITE_TO_SDCARD_TASK_PRIORITY, nullptr);

    xTaskCreate(wind_speed_task, "WindSpeedMonitorTask", WIND_SPEED_TASK_STACK_SIZE, (void*)&wind_task_params, WIND_SPEED_TASK_PRIORITY, &wind_speed_monitor_task_handle);

    xTaskCreate(wind_direction_task, "WindDirectionMonitorTask", WIND_DIRECTION_TASK_STACK_SIZE, (void*)&wind_task_params, WIND_DIRECTION_TASK_PRIORITY, nullptr);



    xTaskCreate(rain_tipping_bucket_task, "RainTippingBucketTask", 512, nullptr, RAIN_TASK_PRIORITY, &rain_tipping_bucket_task_handle);

    vTaskStartScheduler();

    return 0;
}
