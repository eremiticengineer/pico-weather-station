#include "watchdog-tasks.hpp"

void watchdog_task(void* pvParameters) {
    auto* pParam = static_cast<WatchdogTaskParams*>(pvParameters);

    while (true) {
        // While "Wait for all bits" is waiting, the actual watchdog timer may timeout and reboot the mcu
        const EventBits_t bits = xEventGroupWaitBits(
            pParam->watchdog_events,
            WATCHDOG_ALL,
            pdTRUE,                 // Clear bits on exit
            pdTRUE,                 // Wait for all bits
            pdMS_TO_TICKS(1000)     // Check once per second
        );

        // Prevent the watchdog timer from timing out and rebooting the mcu
        if ((bits & WATCHDOG_ALL) == WATCHDOG_ALL) {
            watchdog_update();
        }
    }
}