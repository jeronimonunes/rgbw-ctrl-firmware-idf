#pragma once

#include <Arduino.h>
#include <functional>
#include "hardware.hh"

class ToggleSwitch
{
    static constexpr uint16_t DEBOUNCE = 50;
    static constexpr uint16_t TASK_DELAY_MS = 10;

    const gpio_num_t pin;
    bool state = false;
    bool lastStableState = false;
    unsigned long lastChangeTime = 0;

    TaskHandle_t taskHandle = nullptr;
    std::function<void(bool)> callback;

    [[noreturn]] static void taskLoop(void* arg)
    {
        auto* self = static_cast<ToggleSwitch*>(arg);
        for (;;)
        {
            self->handle(millis());
            vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
        }
    }

public:
    explicit ToggleSwitch(const gpio_num_t pin) : pin(pin)
    {
        pinMode(pin, INPUT_PULLUP);
        state = digitalRead(pin) == HIGH;
        lastStableState = state;
    }

    void begin()
    {
        if (taskHandle == nullptr)
        {
            xTaskCreate(taskLoop, "ToggleSwitchTask", 2048, this, 1, &taskHandle);
        }
    }

    void handle(const unsigned long now)
    {
        if (const bool current = digitalRead(pin) == HIGH;
            current != lastStableState)
        {
            if (now - lastChangeTime >= DEBOUNCE)
            {
                lastStableState = current;
                state = current;
                lastChangeTime = now;

                if (callback)
                    callback(state);
            }
        }
        else
        {
            lastChangeTime = now; // reset debounce window if stable
        }
    }

    void onChanged(const std::function<void(bool)>& cb)
    {
        this->callback = cb;
    }

    bool isOn() const
    {
        return state;
    }

    void stop()
    {
        if (taskHandle)
        {
            vTaskDelete(taskHandle);
            taskHandle = nullptr;
        }
    }
};
