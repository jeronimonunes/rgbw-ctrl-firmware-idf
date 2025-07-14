#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "moving_average.hh"

class Sensor
{
    static constexpr auto PREFERENCES_NAME = "sensor";
    static constexpr auto PREFERENCES_KEY = "f";
    static constexpr auto LOG_TAG = "Sensor";

    static constexpr float VOLTAGE_DIVIDER_R1 = 100.0f; // 10k Ohm
    static constexpr float VOLTAGE_DIVIDER_R2 = 10.0f; // 100k Ohm
    static constexpr float DEFAULT_CALIBRATION_FACTOR =
        (VOLTAGE_DIVIDER_R1 + VOLTAGE_DIVIDER_R2) / VOLTAGE_DIVIDER_R2; // 11.0

public:
#pragma pack(push, 1)
    struct Data
    {
        uint32_t milliVolts; // Raw millivolts
        float calibrationFactor;
    };
#pragma pack(pop)

private:
    const gpio_num_t pin;
    unsigned long lastReadTime = 0;
    MovingAverage<uint32_t, 20> values{};

    static std::mutex& getSensorMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

public:
    explicit Sensor(const gpio_num_t pin) : pin(pin)
    {
    }

    void begin()
    {
        pinMode(pin, INPUT);

        std::lock_guard lock(getSensorMutex());
        values = analogReadMilliVolts(pin);
        ESP_LOGI(LOG_TAG, "Initialized on pin %d with initial value: %lu mV", pin, static_cast<uint32_t>(values));
    }

    void handle(const unsigned long now)
    {
        if (now - lastReadTime < 50) return; // no more than 20 readings per second
        lastReadTime = now;

        std::lock_guard lock(getSensorMutex());
        values += analogReadMilliVolts(pin);
    }

    [[nodiscard]] uint32_t getRawMillivolts() const
    {
        std::lock_guard lock(getSensorMutex());
        return static_cast<uint32_t>(values);
    }

    [[nodiscard]] float getVoltage() const
    {
        return static_cast<float>(getRawMillivolts()) * getCalibrationFactor() / 1000.0f;
    }

    [[nodiscard]] static float getCalibrationFactor()
    {
        Preferences prefs;
        prefs.begin(PREFERENCES_NAME, true);
        const auto value = prefs.getFloat(PREFERENCES_KEY, DEFAULT_CALIBRATION_FACTOR);
        prefs.end();
        return value;
    }

    static void setCalibrationFactor(const float factor)
    {
        Preferences prefs;
        prefs.begin(PREFERENCES_NAME, false);
        prefs.putFloat(PREFERENCES_KEY, factor);
        prefs.end();
    }

    [[nodiscard]] Data getData() const
    {
        return Data{getRawMillivolts(), getCalibrationFactor()};
    }
};
