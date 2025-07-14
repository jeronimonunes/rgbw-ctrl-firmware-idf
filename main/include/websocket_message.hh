#pragma once

#include <array>
#include "device_manager.hh"
#include "ota_handler.hh"
#include "esp_now_handler_controller.hh"

namespace WebSocket
{
#pragma pack(push, 1)
    struct Message
    {
        enum class Type : uint8_t
        {
            ON_HEAP,
            ON_DEVICE_NAME,
            ON_FIRMWARE_VERSION,
            ON_COLOR,
            ON_HTTP_CREDENTIALS,
            ON_BLE_STATUS,
            ON_WIFI_STATUS,
            ON_WIFI_SCAN_STATUS,
            ON_WIFI_DETAILS,
            ON_WIFI_CONNECTION_DETAILS,
            ON_OTA_PROGRESS,
            ON_ALEXA_INTEGRATION_SETTINGS,
            ON_ESP_NOW_DEVICES,
            ON_ESP_NOW_CONTROLLER
        };

        Type type;

        explicit Message(const Type type) : type(type)
        {
        }
    };

    struct ColorMessage : Message
    {
        Output::State state;

        explicit ColorMessage(const Output::State& state)
            : Message(Type::ON_COLOR), state(state)
        {
        }
    };

    struct BleStatusMessage : Message
    {
        BLE::Status status;

        explicit BleStatusMessage(const BLE::Status& status)
            : Message(Type::ON_BLE_STATUS), status(status)
        {
        }
    };

    struct DeviceNameMessage : Message
    {
        std::array<char, DeviceManager::DEVICE_NAME_TOTAL_LENGTH> deviceName = {};

        explicit DeviceNameMessage(const std::array<char, DeviceManager::DEVICE_NAME_TOTAL_LENGTH>& deviceName)
            : Message(Type::ON_DEVICE_NAME)
        {
            std::strncpy(this->deviceName.data(), deviceName.data(), DeviceManager::DEVICE_NAME_MAX_LENGTH);
            this->deviceName[DeviceManager::DEVICE_NAME_MAX_LENGTH] = '\0';
        }

        explicit DeviceNameMessage(const char* deviceName)
            : Message(Type::ON_DEVICE_NAME)
        {
            std::strncpy(this->deviceName.data(), deviceName, DeviceManager::DEVICE_NAME_MAX_LENGTH);
            this->deviceName[DeviceManager::DEVICE_NAME_MAX_LENGTH] = '\0';
        }
    };

    struct HttpCredentialsMessage : Message
    {
        HTTP::Credentials credentials;

        explicit HttpCredentialsMessage(const HTTP::Credentials& credentials)
            : Message(Type::ON_HTTP_CREDENTIALS), credentials(credentials)
        {
        }
    };

    struct WiFiConnectionDetailsMessage : Message
    {
        WiFiConnectionDetails details;

        explicit WiFiConnectionDetailsMessage(const WiFiConnectionDetails& details)
            : Message(Type::ON_WIFI_CONNECTION_DETAILS), details(details)
        {
        }
    };

    struct WiFiDetailsMessage : Message
    {
        WiFiDetails details;

        explicit WiFiDetailsMessage(const WiFiDetails& details)
            : Message(Type::ON_WIFI_DETAILS), details(details)
        {
        }
    };

    struct WiFiStatusMessage : Message
    {
        WiFiStatus status;

        explicit WiFiStatusMessage(const WiFiStatus& status)
            : Message(Type::ON_WIFI_STATUS), status(status)
        {
        }
    };

    struct AlexaIntegrationSettingsMessage : Message
    {
        AlexaIntegration::Settings settings;

        explicit AlexaIntegrationSettingsMessage(const AlexaIntegration::Settings& settings)
            : Message(Type::ON_ALEXA_INTEGRATION_SETTINGS), settings(settings)
        {
        }
    };

    struct OtaProgressMessage : Message
    {
        OTA::State otaState;

        explicit OtaProgressMessage(const OTA::State& otaState)
            : Message(Type::ON_OTA_PROGRESS),
              otaState(otaState)
        {
        }
    };

    struct HeapMessage : Message
    {
        uint32_t freeHeap;

        explicit HeapMessage(const uint32_t freeHeap)
            : Message(Type::ON_HEAP), freeHeap(freeHeap)
        {
        }
    };

    struct EspNowDevicesMessage : Message
    {
        EspNow::DeviceData data;

        explicit EspNowDevicesMessage(const EspNow::DeviceData& data)
            : Message(Type::ON_ESP_NOW_DEVICES), data(data)
        {
        }
    };

    struct EspNowControllerMessage : Message
    {
        std::array<uint8_t, 6> address;

        explicit EspNowControllerMessage(const std::array<uint8_t, 6>& address)
            : Message(Type::ON_ESP_NOW_CONTROLLER), address(address)
        {
        }
    };

    struct FirmwareVersionMessage : Message
    {
        std::array<char, 10> version;

        explicit FirmwareVersionMessage(const std::array<char, 10>& version)
            : Message(Type::ON_FIRMWARE_VERSION), version(version)
        {
        }
    };


#pragma pack(pop)
}
