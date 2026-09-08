#include "sdcard-tasks.hpp"

void write_to_sdcard_task(void* pvParameters) {
    SDCardTaskParams* pParams = static_cast<SDCardTaskParams *>(pvParameters);

    // Must be done in a FreeRTOS task
    pParams->sd_card->init();

    SDCardMessage message;

    while (true)
    {
        if (xQueueReceive(pParams->sdcard_queue, &message, portMAX_DELAY) == pdTRUE) {
            pParams->sd_card->writeAfterInit(message.data);
            printf("wrote to sdcard: %s\n", message.data);
        }
    }
}
