#include "uart-tasks.hpp"

void uart_send_task(void* pvParameters) {
    UARTTaskParams* pParams = static_cast<UARTTaskParams *>(pvParameters);

    // Wait for the sensors to take their first reading
    xEventGroupWaitBits(
        pParams->ready_events,
        ALL_READY,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(5000)
    );    

    while (true) {
        WeatherData snapshot;
        std::string lora_message;
        std::string sdcard_message;

        if (xSemaphoreTake(pParams->weather_data_mutex, portMAX_DELAY)) {
            snapshot = *pParams->weather_data;

            xSemaphoreGive(pParams->weather_data_mutex);

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

        if (xSemaphoreTake(pParams->uart_mutex, pdMS_TO_TICKS(100))) {
            pParams->uart->send(lora_message);

            printf("sent to uart '%s', length=%zu\n", lora_message.c_str(), lora_message.size());

            xSemaphoreGive(pParams->uart_mutex);
        }

        SDCardMessage sdcard_message_to_send {};

        std::strncpy(sdcard_message_to_send.data, lora_message.c_str(), sizeof(sdcard_message_to_send.data) - 1);

        xQueueSend(pParams->sdcard_queue, &sdcard_message_to_send, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(pParams->send_delay_ms));
    }
}
