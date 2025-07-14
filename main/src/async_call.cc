#include "async_call.hh"

#include <esp32-hal.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h> // NOLINT
#include <freertos/task.h>

constexpr auto ASYNC_CALL_TAG = "AsyncCall";

struct AsyncCallArgs
{
    std::function<void()> callback;
    uint32_t delayMs;
};

void doCall(void* args)
{
    const auto* callArgs = static_cast<AsyncCallArgs*>(args);
    delay(callArgs->delayMs);
    callArgs->callback();
    delete callArgs;
    vTaskDelete(nullptr); // Delete this task
}

void async_call(
    std::function<void()> callback,
    const uint32_t usStackDepth,
    const uint32_t delayMs)
{
    if (auto* args = new AsyncCallArgs{std::move(callback), delayMs};
        xTaskCreate(doCall, "AsyncCallTask", usStackDepth, args, 1, nullptr) != pdPASS)
    {
        ESP_LOGE(ASYNC_CALL_TAG, "Failed to create task");
        delete args;
    }
}
