#pragma once

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <string>

#include "alexa_integration.hh"
#include "device_manager.hh"
#include "http_manager.hh"

namespace BLE
{
    class Manager final : public StateJsonFiller, public HTTP::AsyncWebHandlerCreator
    {
        static constexpr auto LOG_TAG = "BleManager";
        static constexpr auto BLE_TIMEOUT_MS = 30000;

        unsigned long bluetoothAdvertisementTimeout = 0;

        const std::array<uint8_t, 4>& advertisementData;
        const DeviceManager& deviceManager;
        const std::vector<Service*> services;

        NimBLEServer* server = nullptr;

    public:
        explicit Manager(
            const std::array<uint8_t, 4>& advertisementData,
            DeviceManager& deviceManager,
            const std::vector<Service*>&& services
        )
            : advertisementData(advertisementData),
              deviceManager(deviceManager),
              services(services)
        {
        }

        void start()
        {
            bluetoothAdvertisementTimeout = millis() + BLE_TIMEOUT_MS;
            if (server != nullptr) return;

            ESP_LOGI(LOG_TAG, "Starting bluetooth");
            BLEDevice::init(deviceManager.getDeviceName());
            server = BLEDevice::createServer();
            server->setCallbacks(new BLEServerCallback());

            for (const auto& service : services)
            {
                service->createServiceAndCharacteristics(server);
            }

            startAdvertising();
        }

        void handle(const unsigned long now)
        {
            handleAdvertisementTimeout(now);
        }

        void stop()
        {
            if (server == nullptr) return;
            ESP_LOGI(LOG_TAG, "Disconnecting all BLE clients");
            for (const auto& connInfo : this->server->getPeerDevices())
            {
                this->server->disconnect(connInfo); // NOLINT
            }
            ESP_LOGI(LOG_TAG, "Clearing all BLE saved pointers");
            for (const auto& service : services)
            {
                service->clearServiceAndCharacteristics();
            }
            ESP_LOGI(LOG_TAG, "Destroying BLE stack");
            BLEDevice::deinit(true);
            this->server = nullptr;
            ESP_LOGI(LOG_TAG, "BLE server stopped");
        }

        static constexpr std::array<uint8_t, 4> buildAdvertisementData(
            const uint16_t manufacturerId, const uint8_t deviceType, const uint8_t deviceSubType)
        {
            std::array<uint8_t, 4> advertisementData = {};
            advertisementData[0] = manufacturerId & 0xFF;
            advertisementData[1] = manufacturerId >> 8 & 0xFF;
            advertisementData[2] = deviceType;
            advertisementData[3] = deviceSubType;
            return advertisementData;
        }

        [[nodiscard]] Status getStatus() const
        {
            if (this->server == nullptr)
                return Status::OFF;
            if (this->server->getConnectedCount() > 0)
                return Status::CONNECTED;
            return Status::ADVERTISING;
        }

        [[nodiscard]] const char* getStatusString() const
        {
            switch (getStatus())
            {
            case Status::OFF:
                return "OFF";
            case Status::ADVERTISING:
                return "ADVERTISING";
            case Status::CONNECTED:
                return "CONNECTED";
            default:
                return "UNKNOWN";
            }
        }

        void fillState(const JsonObject& root) const override
        {
            const auto ble = root["ble"].to<JsonObject>();
            ble["status"] = getStatusString();
        }

        AsyncWebHandler* createAsyncWebHandler() override
        {
            return new AsyncRestWebHandler(this);
        }

    private:
        void startAdvertising()
        {
            const auto advertising = this->server->getAdvertising();

            NimBLEAdvertisementData scanRespData;
            scanRespData.setName(deviceManager.getDeviceName());
            advertising->setScanResponseData(scanRespData);

            advertising->setManufacturerData(advertisementData.data(), advertisementData.size());
            advertising->start();
            bluetoothAdvertisementTimeout = millis() + BLE_TIMEOUT_MS;
            ESP_LOGI(LOG_TAG, "BLE advertising started with device name: %s", deviceManager.getDeviceName());
        }

        void handleAdvertisementTimeout(const unsigned long now)
        {
            if (this->getStatus() == Status::CONNECTED)
            {
                bluetoothAdvertisementTimeout = now + BLE_TIMEOUT_MS;
            }
            else if (now > bluetoothAdvertisementTimeout && this->server != nullptr)
            {
                ESP_LOGW(LOG_TAG, "No BLE client connected for %d ms, stopping BLE server.", BLE_TIMEOUT_MS);
                this->stop();
            }
        }

        class AsyncRestWebHandler final : public AsyncWebHandler
        {
            Manager* bleManager;

        public:
            explicit AsyncRestWebHandler(Manager* bleManager)
                : bleManager(bleManager)
            {
            }

            bool canHandle(AsyncWebServerRequest* request) const override
            {
                return request->method() == HTTP_GET &&
                    request->url() == HTTP::Endpoints::BLUETOOTH;
            }

            void handleRequest(AsyncWebServerRequest* request) override
            {
                if (!request->hasParam("state"))
                    return sendMessageJsonResponse(request, "Missing 'state' parameter");

                auto state = request->getParam("state")->value() == "on";
                request->onDisconnect([this, state]
                {
                    if (state)
                        bleManager->start();
                    else
                        bleManager->stop();
                });

                if (state)
                    return sendMessageJsonResponse(request, "Bluetooth enabled");
                return sendMessageJsonResponse(request, "Bluetooth disabled");
            }
        };

        class BLEServerCallback final : public NimBLEServerCallbacks
        {
            void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override
            {
                pServer->startAdvertising(); // NOLINT
            }
        };
    };
}
