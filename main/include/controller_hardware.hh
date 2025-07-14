#pragma once

#include <Arduino.h>
#include <optional>

namespace ControllerHardware
{
    namespace Pin
    {
        namespace Input
        {
            constexpr auto VOLTAGE = GPIO_NUM_34;
        }

        namespace Button
        {
            constexpr auto BUTTON1 = GPIO_NUM_0;
        }

        namespace BoardLed
        {
            constexpr auto RED = GPIO_NUM_21;
            constexpr auto GREEN = GPIO_NUM_17;
            constexpr auto BLUE = GPIO_NUM_4;
        }

        namespace Output
        {
            constexpr auto RED = GPIO_NUM_13;
            constexpr auto GREEN = GPIO_NUM_16;
            constexpr auto BLUE = GPIO_NUM_19;
            constexpr auto WHITE = GPIO_NUM_18;
        }

        namespace Header
        {
            namespace H1
            {
                constexpr auto P1 = GPIO_NUM_27;
                constexpr auto P2 = GPIO_NUM_26;
                constexpr auto P3 = GPIO_NUM_25;
                constexpr auto P4 = GPIO_NUM_33;
            }

            namespace H2
            {
                constexpr auto RX = GPIO_NUM_3;
                constexpr auto TX = GPIO_NUM_1;
            }
        }
    }

    inline std::optional<uint8_t> getPwmChannel(const gpio_num_t pin)
    {
        switch (pin)
        {
        case Pin::BoardLed::RED: return 0;
        case Pin::BoardLed::GREEN: return 1;
        case Pin::BoardLed::BLUE: return 2;
        case Pin::Output::RED: return 3;
        case Pin::Output::GREEN: return 4;
        case Pin::Output::BLUE: return 5;
        case Pin::Output::WHITE: return 6;
        default: return std::nullopt;
        }
    }
}
