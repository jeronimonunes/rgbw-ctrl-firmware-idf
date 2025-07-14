#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <algorithm>
#include <Preferences.h>
#include <NimBLEServer.h>

namespace EspNow
{
#pragma pack(push, 1)
    struct Device
    {
        static constexpr auto NAME_MAX_LENGTH = 23;
        static constexpr auto NAME_TOTAL_LENGTH = 24;
        static constexpr uint8_t MAC_SIZE = 6;

        std::array<char, NAME_TOTAL_LENGTH> name;
        std::array<uint8_t, MAC_SIZE> address;

        bool operator==(const Device& other) const
        {
            return name == other.name && address == other.address;
        }

        bool operator!=(const Device& other) const
        {
            return name != other.name && address != other.address;
        }
    };

    static_assert(sizeof(Device) == Device::NAME_TOTAL_LENGTH + Device::MAC_SIZE,
                  "Unexpected EspNowDevice size");

    struct DeviceData
    {
        static constexpr uint8_t MAX_DEVICES_PER_MESSAGE = 10;

        uint8_t deviceCount = 0;
        std::array<Device, MAX_DEVICES_PER_MESSAGE> devices = {};

        bool operator==(const DeviceData& other) const
        {
            return deviceCount == other.deviceCount && devices == other.devices;
        }

        bool operator!=(const DeviceData& other) const
        {
            return deviceCount != other.deviceCount || devices != other.devices;
        }
    };
#pragma pack(pop)

    class ControllerHandler final : public BLE::Service, public StateJsonFiller
    {
        static constexpr auto LOG_TAG = "ControllerEspNowHandler";

        static constexpr auto PREFERENCES_NAME = "esp-now";
        static constexpr auto PREFERENCES_COUNT_KEY = "devCount";
        static constexpr auto PREFERENCES_DATA_KEY = "devData";

        DeviceData deviceData = {};

    public:
        void begin()
        {
            restoreDevices();
        }

        [[nodiscard]] DeviceData getDeviceData() const
        {
            std::lock_guard lock(getMutex());
            return deviceData;
        }

        void setDeviceData(const DeviceData& data)
        {
            std::lock_guard lock(getMutex());
            deviceData = data;
            persistDevices();
        }

        bool isMacAllowed(const uint8_t* mac)
        {
            std::lock_guard lock(getMutex());
            for (uint8_t i = 0; i < deviceData.deviceCount; ++i)
            {
                if (const auto& [name, address] = deviceData.devices[i];
                    std::equal(address.begin(), address.end(), mac))
                    return true;
            }
            return false;
        }

        std::optional<Device> findDeviceByMac(const uint8_t* mac) const
        {
            std::lock_guard lock(getMutex());
            for (uint8_t i = 0; i < deviceData.deviceCount; ++i)
            {
                if (const auto& device = deviceData.devices[i];
                    std::equal(device.address.begin(), device.address.end(), mac))
                    return device;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Device> findDeviceByName(const std::string_view name) const
        {
            std::lock_guard lock(getMutex());
            for (uint8_t i = 0; i < deviceData.deviceCount; ++i)
            {
                const auto& device = deviceData.devices[i];
                const auto& devName = device.name;
                if (std::string_view(devName.data(), strnlen(devName.data(), devName.size())) == name)
                    return device;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::vector<uint8_t> getDevicesBuffer()
        {
            std::lock_guard lock(getMutex());
            std::vector<uint8_t> buffer;
            buffer.reserve(1 + deviceData.deviceCount * sizeof(Device));
            buffer.push_back(deviceData.deviceCount);
            for (const auto& [name, address] : deviceData.devices)
            {
                buffer.insert(buffer.end(), name.begin(), name.end());
                buffer.insert(buffer.end(), address.begin(), address.end());
            }
            return buffer;
        }

        void setDevicesBuffer(const uint8_t* data, const size_t length)
        {
            if (!data || length < 1) return;

            const uint8_t count = data[0];
            if (const size_t expectedSize = 1 + count * sizeof(Device);
                length < expectedSize)
                return;

            DeviceData newData = {};
            newData.deviceCount = std::min(count, DeviceData::MAX_DEVICES_PER_MESSAGE);

            for (uint8_t i = 0; i < newData.deviceCount; ++i)
            {
                const size_t offset = 1 + i * sizeof(Device);
                std::copy_n(&data[offset], Device::NAME_TOTAL_LENGTH, newData.devices[i].name.begin());
                std::copy_n(&data[offset + Device::NAME_TOTAL_LENGTH], Device::MAC_SIZE,
                            newData.devices[i].address.begin());
            }

            setDeviceData(newData);
        }

        void createServiceAndCharacteristics(NimBLEServer* server) override
        {
            const auto bleService = server->createService(BLE::UUID::ESP_NOW_CONTROLLER_SERVICE);
            bleService->createCharacteristic(
                BLE::UUID::ESP_NOW_REMOTES_CHARACTERISTIC,
                READ | WRITE
            )->setCallbacks(new EspNowDevicesCallback(this));
            bleService->start();
        }

        void clearServiceAndCharacteristics() override
        {
            ESP_LOGI(LOG_TAG, "No BLE pointers to be cleared");
        }

        void fillState(const JsonObject& root) const override
        {
            const auto espNow = root["espNow"].to<JsonObject>();
            const auto arr = espNow["devices"].to<JsonArray>();
            std::lock_guard lock(getMutex());
            for (uint8_t i = 0; i < deviceData.deviceCount && i < DeviceData::MAX_DEVICES_PER_MESSAGE; ++i)
            {
                const auto& [name, mac] = deviceData.devices[i];
                char macStr[18];
                snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                macStr[sizeof(macStr) - 1] = '\0';
                const auto& obj = arr.add<JsonObject>();
                obj["name"] = name.data();
                obj["address"] = macStr;
            }
        }

    private:
        static std::mutex& getMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        void persistDevices() const
        {
            if (Preferences prefs; prefs.begin(PREFERENCES_NAME, false))
            {
                const auto dataSize = deviceData.deviceCount * sizeof(Device);
                prefs.putUInt(PREFERENCES_COUNT_KEY, deviceData.deviceCount);
                prefs.putBytes(PREFERENCES_DATA_KEY, deviceData.devices.data(), dataSize);
                prefs.end();
                ESP_LOGI(LOG_TAG, "Devices saved to Preferences");
            }
            else
            {
                ESP_LOGE(LOG_TAG, "Failed to open Preferences for saving");
            }
        }

        void restoreDevices()
        {
            if (Preferences prefs; prefs.begin(PREFERENCES_NAME, true))
            {
                deviceData.deviceCount = prefs.getUInt(PREFERENCES_COUNT_KEY, 0);
                if (const auto dataSize = deviceData.deviceCount * sizeof(Device);
                    prefs.getBytesLength(PREFERENCES_DATA_KEY) == dataSize)
                {
                    deviceData.devices = {};
                    prefs.getBytes(PREFERENCES_DATA_KEY, deviceData.devices.data(), dataSize);
                    ESP_LOGI(LOG_TAG, "Devices restored from Preferences");
                }
                prefs.end();
            }
            else
            {
                ESP_LOGE(LOG_TAG, "Failed to open Preferences for reading");
            }
        }

        class EspNowDevicesCallback final : public NimBLECharacteristicCallbacks
        {
            ControllerHandler* espNowHandler;

        public:
            explicit EspNowDevicesCallback(ControllerHandler* espNowHandler)
                : espNowHandler(espNowHandler)
            {
            }

            void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
            {
                const auto value = pCharacteristic->getValue();
                espNowHandler->setDevicesBuffer(value.data(), value.size());
            }

            void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
            {
                const auto value = espNowHandler->getDevicesBuffer();
                pCharacteristic->setValue(value.data(), value.size());
            }
        };
    };
}
