#pragma once

#include <nvs_flash.h>

#include "NimBLEServer.h"
#include "NimBLEService.h"
#include "NimBLECharacteristic.h"
#include "throttled_value.hh"
#include "ble_service.hh"
#include "http_manager.hh"
#include "state_json_filler.hh"
#include "async_call.hh"
#include "sensor.hh"

class DeviceManager final : public BLE::Service, public StateJsonFiller, public HTTP::AsyncWebHandlerCreator
{
public:
    static constexpr auto FIRMWARE_VERSION = "5.1.1";

    static constexpr auto DEVICE_BASE_NAME = "rgbw-ctrl-";
    static constexpr auto DEVICE_NAME_MAX_LENGTH = 28;
    static constexpr auto DEVICE_NAME_TOTAL_LENGTH = DEVICE_NAME_MAX_LENGTH + 1;

private:
    static constexpr auto LOG_TAG = "DeviceManager";
    static constexpr auto PREFERENCES_NAME = "device-config";

    Sensor sensor{ControllerHardware::Pin::Input::VOLTAGE};

    NimBLECharacteristic* bleDeviceNameCharacteristic = nullptr;
    NimBLECharacteristic* bleDeviceHeapCharacteristic = nullptr;
    NimBLECharacteristic* bleInputVoltageCharacteristic = nullptr;

    mutable std::array<char, DEVICE_NAME_TOTAL_LENGTH> deviceName = {};
    ThrottledValue<uint32_t> heapNotificationThrottle{500};

    unsigned long lastVoltageNotification = 0;

public:
    void begin()
    {
        sensor.begin();
        WiFi.mode(WIFI_MODE_STA); // NOLINT
    }

    void handle(const unsigned long now)
    {
        sensor.handle(now);
        sendHeapNotification(now);
        sendInputVoltageNotification(now);
    }

    char* getDeviceName() const
    {
        std::lock_guard lock(getDeviceNameMutex());
        if (deviceName[0] == '\0')
        {
            loadDeviceName(deviceName.data());
        }
        return deviceName.data();
    }

    std::array<char, DEVICE_NAME_TOTAL_LENGTH> getDeviceNameArray() const
    {
        std::lock_guard lock(getDeviceNameMutex());
        if (deviceName[0] == '\0')
        {
            loadDeviceName(deviceName.data());
        }
        return deviceName;
    }

    void setDeviceName(const char* name) // NOLINT
    {
        if (!name || name[0] == '\0') return;

        std::lock_guard lock(getDeviceNameMutex());

        char safeName[DEVICE_NAME_TOTAL_LENGTH];
        std::strncpy(safeName, name, DEVICE_NAME_MAX_LENGTH);
        safeName[DEVICE_NAME_MAX_LENGTH] = '\0';

        if (std::strncmp(deviceName.data(), safeName, DEVICE_NAME_MAX_LENGTH) == 0)
            return;

        Preferences prefs;
        prefs.begin(PREFERENCES_NAME, false);
        prefs.putString("deviceName", safeName);
        prefs.end();

        deviceName[0] = '\0'; // Invalidate cached name
        WiFiClass::setHostname(safeName);
        WiFi.reconnect();

        std::lock_guard bleLock(getBleMutex());
        if (bleDeviceHeapCharacteristic == nullptr) return;
        ESP_LOGI(LOG_TAG, "Notifying device name change via BLE");
        const auto len = std::min(strlen(safeName), static_cast<size_t>(DEVICE_NAME_MAX_LENGTH));
        bleDeviceNameCharacteristic->setValue(reinterpret_cast<uint8_t*>(safeName), len);
        bleDeviceNameCharacteristic->notify(); // NOLINT
    }

    AsyncWebHandler* createAsyncWebHandler() override
    {
        return new AsyncRestWebHandler(this);
    }

    void fillState(const JsonObject& root) const override
    {
        root["deviceName"] = getDeviceName();
        root["firmwareVersion"] = FIRMWARE_VERSION;
        root["heap"] = esp_get_free_heap_size();
    }

    void createServiceAndCharacteristics(NimBLEServer* server) override
    {
        ESP_LOGI(LOG_TAG, "Creating BLE services and characteristics");
        std::lock_guard bleLock(getBleMutex());
        const auto service = server->createService(BLE::UUID::DEVICE_DETAILS_SERVICE);

        service->createCharacteristic(
            BLE::UUID::DEVICE_RESTART_CHARACTERISTIC,
            WRITE
        )->setCallbacks(new RestartCallback());

        bleDeviceNameCharacteristic = service->createCharacteristic(
            BLE::UUID::DEVICE_NAME_CHARACTERISTIC,
            WRITE | READ | NOTIFY
        );
        bleDeviceNameCharacteristic->setCallbacks(new DeviceNameCallback(this));

        service->createCharacteristic(
            BLE::UUID::FIRMWARE_VERSION_CHARACTERISTIC,
            READ
        )->setCallbacks(new FirmwareVersionCallback());

        bleDeviceHeapCharacteristic = service->createCharacteristic(
            BLE::UUID::DEVICE_HEAP_CHARACTERISTIC,
            NOTIFY
        );

        bleInputVoltageCharacteristic = service->createCharacteristic(
            BLE::UUID::INPUT_VOLTAGE_CHARACTERISTIC,
            READ | WRITE | NOTIFY
        );
        bleInputVoltageCharacteristic->setCallbacks(new InputVoltageCallback(sensor));

        service->start();
        ESP_LOGI(LOG_TAG, "DONE creating BLE services and characteristics");
    }

    void clearServiceAndCharacteristics() override
    {
        ESP_LOGI(LOG_TAG, "Clearing BLE services and characteristics");
        std::lock_guard bleLock(getBleMutex());
        bleDeviceNameCharacteristic = nullptr;
        bleDeviceHeapCharacteristic = nullptr;
        bleInputVoltageCharacteristic = nullptr;
        ESP_LOGI(LOG_TAG, "DONE clearing BLE services and characteristics");
    }

    static std::mutex& getBleMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

private:
    static const char* loadDeviceName(char* deviceName)
    {
        Preferences prefs;
        prefs.begin(PREFERENCES_NAME, true);
        if (prefs.isKey("deviceName"))
        {
            prefs.getString("deviceName", deviceName, DEVICE_NAME_TOTAL_LENGTH);
            prefs.end();
            return deviceName;
        }
        prefs.end();
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(deviceName, DEVICE_NAME_TOTAL_LENGTH,
                 "%s%02X%02X%02X", DEVICE_BASE_NAME, mac[3], mac[4], mac[5]);
        return deviceName;
    }

    static std::mutex& getDeviceNameMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    void sendHeapNotification(const unsigned long now)
    {
        std::lock_guard bleLock(getBleMutex());
        if (bleDeviceHeapCharacteristic == nullptr) return;

        auto state = ESP.getFreeHeap();
        if (!heapNotificationThrottle.shouldSend(now, state))
            return;
        bleDeviceHeapCharacteristic->setValue(reinterpret_cast<uint8_t*>(&state), sizeof(state));
        if (bleDeviceHeapCharacteristic->notify())
        {
            heapNotificationThrottle.setLastSent(now, state);
        }
    }

    void sendInputVoltageNotification(const unsigned long now)
    {
        if (now - lastVoltageNotification < 1000) return; // no more than 1 notification per second
        lastVoltageNotification = now;

        std::lock_guard bleLock(getBleMutex());
        if (bleInputVoltageCharacteristic == nullptr) return;

        auto state = sensor.getData();
        bleInputVoltageCharacteristic->setValue(reinterpret_cast<uint8_t*>(&state), sizeof(state));
        bleInputVoltageCharacteristic->notify(); // NOLINT
    }

    class RestartCallback final : public NimBLECharacteristicCallbacks
    {
    public:
        void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            if (pCharacteristic->getValue() == "RESTART_NOW")
            {
                ESP_LOGW(LOG_TAG, "Device restart requested via BLE.");
                async_call([this]
                {
                    esp_restart();
                }, 2048, 50);
            }
            else
            {
                ESP_LOGW(LOG_TAG, "Device restart ignored: invalid value received.");
            }
        }
    };

    class DeviceNameCallback final : public NimBLECharacteristicCallbacks
    {
        DeviceManager* deviceManager;

    public:
        explicit DeviceNameCallback(DeviceManager* deviceManager) : deviceManager(deviceManager)
        {
        }

        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            const auto name = deviceManager->getDeviceName();
            const auto len = std::min(strlen(name), static_cast<size_t>(DEVICE_NAME_MAX_LENGTH));
            pCharacteristic->setValue(reinterpret_cast<uint8_t*>(const_cast<char*>(name)), len);
        }

        void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            const auto value = pCharacteristic->getValue();
            const auto data = value.data();
            const auto deviceName = reinterpret_cast<const char*>(data);

            if (const auto length = pCharacteristic->getLength(); length == 0 || length > DEVICE_NAME_MAX_LENGTH)
            {
                ESP_LOGE(LOG_TAG, "Invalid device name length: %d", static_cast<int>(length));
                return;
            }

            deviceManager->setDeviceName(deviceName);
        }
    };

    class FirmwareVersionCallback final : public NimBLECharacteristicCallbacks
    {
    public:
        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            pCharacteristic->setValue(FIRMWARE_VERSION);
        }
    };

    class InputVoltageCallback final : public NimBLECharacteristicCallbacks
    {
        Sensor& sensor;

    public:
        explicit InputVoltageCallback(Sensor& sensor) : sensor(sensor)
        {
        }

        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            const auto data = sensor.getData();
            pCharacteristic->setValue(reinterpret_cast<const uint8_t*>(&data), sizeof(data));
        }

        void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            if (pCharacteristic->getValue().size() != sizeof(float))
            {
                ESP_LOGE(LOG_TAG, "Invalid calibration factor size");
                return;
            }
            float factor = 0;
            memcpy(&factor, pCharacteristic->getValue().data(), sizeof(float));
            Sensor::setCalibrationFactor(factor);
            ESP_LOGI(LOG_TAG, "Calibration factor updated via BLE: %.3f", factor);
        }
    };

    class AsyncRestWebHandler final : public AsyncWebHandler
    {
        DeviceManager* deviceManager;

    public:
        explicit AsyncRestWebHandler(DeviceManager* deviceManager)
            : deviceManager(deviceManager)
        {
        }

        bool canHandle(AsyncWebServerRequest* request) const override
        {
            return request->method() == HTTP_GET &&
            (request->url() == HTTP::Endpoints::SYSTEM_RESTART ||
                request->url() == HTTP::Endpoints::SYSTEM_RESET);
        }

        void handleRequest(AsyncWebServerRequest* request) override
        {
            if (request->url() == HTTP::Endpoints::SYSTEM_RESET)
            {
                return handleResetRequest(request);
            }
            return handleRestartRequest(request);
        }

        void handleRestartRequest(AsyncWebServerRequest* request) const
        {
            request->onDisconnect([this]
            {
                async_call([]
                {
                    esp_restart();
                }, 2048, 0);
            });
            return sendMessageJsonResponse(request, "Restarting...");
        }

        void handleResetRequest(AsyncWebServerRequest* request) const
        {
            request->onDisconnect([this]
            {
                async_call([]
                {
                    nvs_flash_erase();
                    esp_restart();
                }, 4096, 0);
            });
            return sendMessageJsonResponse(request, "Resetting to factory defaults...");
        }
    };
};
