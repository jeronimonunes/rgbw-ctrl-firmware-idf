#pragma once

#include <Arduino.h>
#include <functional>

class PushButton
{
    static constexpr auto LOG_TAG = "PushButton";
    gpio_num_t pin;

    unsigned long lastDown = 0;
    unsigned long lastChange = 0;
    bool lastState = HIGH;
    bool longPressHandled = false;
    unsigned long longPressThresholdMs = 2500;
    unsigned long debounceDelayMs = 50;

    std::function<void()> longPressCallback;
    std::function<void()> shortPressCallback;

    static void maybeInvoke(const std::function<void()>& cb)
    {
        if (cb) cb();
    }

public:
    explicit PushButton(const gpio_num_t pin, const unsigned long thresholdMs = 2500)
        : pin(pin), longPressThresholdMs(thresholdMs)
    {
        pinMode(this->pin, INPUT_PULLUP);
    }

    void setLongPressCallback(const std::function<void()>& callback)
    {
        longPressCallback = callback;
    }

    void setShortPressCallback(const std::function<void()>& callback)
    {
        shortPressCallback = callback;
    }

    void handle(const unsigned long now)
    {
        const bool currentState = digitalRead(pin);

        if (currentState != lastState && now - lastChange < debounceDelayMs)
        {
            // Ignore bounce
            return;
        }

        if (lastState == HIGH && currentState == LOW)
        {
            // Button just pressed
            lastDown = now;
            longPressHandled = false;
            lastChange = now;
        }
        else if (lastState == LOW && currentState == LOW)
        {
            // Button is being held
            if (!longPressHandled && now - lastDown >= longPressThresholdMs)
            {
                maybeInvoke(longPressCallback);
                longPressHandled = true;
            }
        }
        else if (lastState == LOW && currentState == HIGH)
        {
            // Button just released
            if (const auto pressDuration = now - lastDown;
                !longPressHandled && pressDuration < longPressThresholdMs)
            {
                maybeInvoke(shortPressCallback);
            }
            lastChange = now;
        }

        lastState = currentState;
    }
};
