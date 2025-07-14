#pragma once

#include <NimBLEDevice.h>

namespace BLE
{
    enum class Status :uint8_t
    {
        OFF,
        ADVERTISING,
        CONNECTED
    };

    class Service
    {
    public:
        virtual ~Service() = default;
        virtual void createServiceAndCharacteristics(NimBLEServer* server) = 0;
        virtual void clearServiceAndCharacteristics() = 0;
    };

    namespace UUID
    {
        static constexpr auto DEVICE_DETAILS_SERVICE = "12345678-1234-1234-1234-1234567890a0";
        static constexpr auto DEVICE_RESTART_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0000";
        static constexpr auto DEVICE_NAME_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0001";
        static constexpr auto FIRMWARE_VERSION_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0002";
        static constexpr auto DEVICE_HEAP_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0003";
        static constexpr auto INPUT_VOLTAGE_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0004";

        static constexpr auto HTTP_DETAILS_SERVICE = "12345678-1234-1234-1234-1234567890a1";
        static constexpr auto HTTP_CREDENTIALS_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0005";

        static constexpr auto OUTPUT_SERVICE = "12345678-1234-1234-1234-1234567890a2";
        static constexpr auto OUTPUT_COLOR_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0006";

        static constexpr auto ALEXA_SERVICE = "12345678-1234-1234-1234-1234567890a3";
        static constexpr auto ALEXA_SETTINGS_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0007";

        static constexpr auto ESP_NOW_CONTROLLER_SERVICE = "12345678-1234-1234-1234-1234567890a4";
        static constexpr auto ESP_NOW_REMOTES_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0008";

        static constexpr auto ESP_NOW_REMOTE_SERVICE = "12345678-1234-1234-1234-1234567890a5";
        static constexpr auto ESP_NOW_CONTROLLER_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee0009";

        static constexpr auto WIFI_SERVICE = "12345678-1234-1234-1234-1234567890a6";
        static constexpr auto WIFI_DETAILS_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee000a";
        static constexpr auto WIFI_STATUS_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee000b";
        static constexpr auto WIFI_SCAN_STATUS_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee000c";
        static constexpr auto WIFI_SCAN_RESULT_CHARACTERISTIC = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeee000d";
    }
}
