#pragma once

#include <iot_knob.h>

class RotaryEncoderManager
{
    static constexpr const char* LOG_TAG = "RotaryEncoderManager";
    knob_handle_t knob;

    std::function<void()> turnLeftCallback;
    std::function<void()> turnRightCallback;

    static void _knob_left_cb(void* arg, void* data)
    {
        if (const auto self = static_cast<RotaryEncoderManager*>(data);
            self->turnLeftCallback)
            self->turnLeftCallback();
    }

    static void _knob_right_cb(void* arg, void* data)
    {
        if (const auto self = static_cast<RotaryEncoderManager*>(data);
            self->turnRightCallback)
            self->turnRightCallback();
    }

public:
    explicit RotaryEncoderManager(const gpio_num_t pinA,
                                  const gpio_num_t pinB,
                                  const gpio_num_t groundPin = GPIO_NUM_NC,
                                  const gpio_num_t vccPin = GPIO_NUM_NC
    )
    {
        const knob_config_t cfg = {
            .default_direction = 0,
            .gpio_encoder_a = static_cast<uint8_t>(pinA),
            .gpio_encoder_b = static_cast<uint8_t>(pinB),
            .enable_power_save = true
        };
        knob = iot_knob_create(&cfg);
        iot_knob_register_cb(knob, KNOB_LEFT, _knob_left_cb, this);
        iot_knob_register_cb(knob, KNOB_RIGHT, _knob_right_cb, this);

        if (groundPin != GPIO_NUM_NC)
        {
            pinMode(groundPin, OUTPUT);
            digitalWrite(groundPin, LOW);
        }
        if (vccPin != GPIO_NUM_NC)
        {
            pinMode(vccPin, OUTPUT);
            digitalWrite(vccPin, HIGH);
        }
    }

    ~RotaryEncoderManager()
    {
        iot_knob_delete(knob);
    }


    void begin()
    {
        // think
    }

    void onTurnLeft(const std::function<void()>& callback)
    {
        this->turnLeftCallback = callback;
    }

    void onTurnRight(const std::function<void()>& callback)
    {
        this->turnRightCallback = callback;
    }

    // void onPressed(const std::function<void(unsigned long)>& callback)
    // {
    //     this->rotaryEncoder.onPressed(callback);
    // }
};
