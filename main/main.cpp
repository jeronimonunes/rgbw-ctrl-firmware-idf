#include <Arduino.h>
#include <AsyncTCP.h>

#include "task_monitor.hpp"

void monitor_task(void *pvParameters)
{
    while (1)
    {
        print_task_stats();
        vTaskDelay(pdMS_TO_TICKS(5000)); // a cada 5s
    }
}

extern "C" void app_main()
{
    // initArduino();
    xTaskCreate(monitor_task, "monitor_task", 2048, NULL, 5, NULL);
}

IPAddress AsyncClient::remoteIP() const {
    return IPAddress(getRemoteAddress());
}