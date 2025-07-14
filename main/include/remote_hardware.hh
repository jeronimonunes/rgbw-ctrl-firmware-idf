#pragma once

#include <Arduino.h>

namespace RemoteHardware::Pin
{
    namespace Button
    {
        constexpr auto BUTTON1 = GPIO_NUM_0;
    }

    namespace Header
    {
        namespace H1
        {
            constexpr auto P1 = GPIO_NUM_27; // 100nf
            constexpr auto P2 = GPIO_NUM_26; // 100nf
            constexpr auto P3 = GPIO_NUM_25; // 100nf
            constexpr auto P4 = GPIO_NUM_33; // 100nf
        }

        namespace H2
        {
            constexpr auto P1 = GPIO_NUM_NC; // 3.3v
            constexpr auto P2 = GPIO_NUM_1; // TX
            constexpr auto P3 = GPIO_NUM_3; // RX
            constexpr auto P4 = GPIO_NUM_NC; // GND
            constexpr auto P5 = GPIO_NUM_21;
            constexpr auto P6 = GPIO_NUM_19;
            constexpr auto P7 = GPIO_NUM_18; // 100nf
            constexpr auto P8 = GPIO_NUM_17; // 100nf
            constexpr auto P9 = GPIO_NUM_16; // 100nf
        }
    }
}
