#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>

#include "controller_hardware.hh"

class Light
{
public:
#pragma pack(push, 1)
    struct State
    {
        bool on = false;
        uint8_t value = 0;

        bool operator==(const State &other) const
        {
            return on == other.on && value == other.value;
        }

        bool operator!=(const State &other) const
        {
            return on != other.on || value != other.value;
        }

        void toJson(const JsonObject &to) const
        {
            to["on"] = on;
            to["value"] = value;
        }
    };
#pragma pack(pop)

    static constexpr auto PREFERENCES_NAME = "light";
    static constexpr auto LOG_TAG = "Light";

    static constexpr uint8_t ON_VALUE = 255;
    static constexpr uint8_t OFF_VALUE = 0;

    static constexpr uint8_t MIN_BRIGHTNESS = OFF_VALUE + 1;
    static constexpr uint8_t MAX_BRIGHTNESS = ON_VALUE;

    void setup()
    {
        prefs.begin(PREFERENCES_NAME, false);
        pinMode(pin, OUTPUT);
        ledcAttach(pin, PWM_FREQUENCY, PWM_RESOLUTION);
        restore();
    }

    void handle(const unsigned long now)
    {
        if (state != lastPersistedState && now - lastPersistTime >= PERSIST_DEBOUNCE_MS)
        {
            prefs.putBool(onKey, state.on);
            prefs.putUChar(valueKey, state.value);
            lastPersistedState = state;
            lastPersistTime = now;
        }
    }

private:
    static constexpr uint32_t PWM_FREQUENCY = 25000;
    static constexpr uint8_t PWM_RESOLUTION = 8;
    static constexpr unsigned long PERSIST_DEBOUNCE_MS = 500;

    bool invert;
    gpio_num_t pin;
    State state;

    char onKey[5] = "";
    char valueKey[5] = "";

    std::optional<uint8_t> lastWrittenValue = std::nullopt;

    Preferences prefs;
    State lastPersistedState;
    unsigned long lastPersistTime = 0;

    void update()
    {
        const auto &channel = ControllerHardware::getPwmChannel(pin);
        const auto duty = state.on ? state.value : OFF_VALUE;

        if (uint8_t outputValue = invert ? MAX_BRIGHTNESS - duty : duty;
            lastWrittenValue != outputValue)
        {
            ledcWrite(channel.value(), outputValue);
            lastWrittenValue = outputValue;
        }
    }

    void restore()
    {
        state.on = prefs.getBool(onKey, false);
        state.value = prefs.getUChar(valueKey, OFF_VALUE);
        update();
    }

    static uint8_t perceptualBrightnessStep(const uint8_t currentValue, const bool increase)
    {
        constexpr float gamma = 2.2f;
        float linear = pow(
            static_cast<float>(currentValue) / static_cast<float>(MAX_BRIGHTNESS),
            1.0f / gamma);
        linear += increase ? 0.05f : -0.05f;
        linear = std::clamp(linear, 0.0f, 1.0f);
        const auto value = lround(
            pow(linear, gamma) * static_cast<float>(MAX_BRIGHTNESS));
        return std::clamp(static_cast<uint8_t>(value), MIN_BRIGHTNESS, MAX_BRIGHTNESS);
    }

public:
    explicit Light(const gpio_num_t pin, const bool invert = false) : invert(invert), pin(pin)
    {
        snprintf(onKey, sizeof(onKey), "%02uo", static_cast<unsigned>(pin));
        snprintf(valueKey, sizeof(valueKey), "%02uv", static_cast<unsigned>(pin));
    }

    ~Light()
    {
        prefs.end();
    }

    void toggle()
    {
        state.on = !state.on;
        if (state.on && state.value == OFF_VALUE)
            state.value = MAX_BRIGHTNESS;
        update();
    }

    void setValue(const uint8_t value)
    {
        state.value = value;
        update();
    }

    void setOn(const bool stateFlag)
    {
        state.on = stateFlag;
        update();
    }

    void increaseBrightness()
    {
        if (state.value == MAX_BRIGHTNESS)
            return;

        const auto step = perceptualBrightnessStep(state.value, true);

        ESP_LOGI(LOG_TAG, "Increasing brightness to %u", step);
        state.value = step;
        update();
    }

    void decreaseBrightness()
    {
        if (isOff() || state.value == MIN_BRIGHTNESS)
            return;

        const auto step = perceptualBrightnessStep(state.value, false);

        ESP_LOGI(LOG_TAG, "Decreasing brightness to %u", step);
        state.value = step;
        update();
    }

    void setState(const State &state)
    {
        this->state = state;
        update();
    }

    void makeVisible()
    {
        state.on = true;
        if (state.value == OFF_VALUE)
            state.value = MAX_BRIGHTNESS;
    }

    void toJson(const JsonObject &to) const
    {
        state.toJson(to);
    }

    [[nodiscard]] bool isOn() const { return state.on; }
    [[nodiscard]] bool isOff() const { return !state.on; }
    [[nodiscard]] bool isVisible() const { return state.on && state.value > 0; }
    [[nodiscard]] uint8_t getValue() const { return state.value; }
    [[nodiscard]] State getState() const { return state; }
};
