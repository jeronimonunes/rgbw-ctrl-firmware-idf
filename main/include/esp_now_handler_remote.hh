#pragma once

#include <array>
#include <mutex>
#include <esp_now.h>
#include <algorithm>
#include <Preferences.h>
#include <NimBLEServer.h>

#include "ble_service.hh"
#include "esp_now_handler.hh"
#include "state_json_filler.hh"

namespace EspNow
{
    class RemoteHandler final : public BLE::Service, public StateJsonFiller
    {
        static constexpr auto LOG_TAG = "RemoteEspNowHandler";

        static constexpr auto MAC_LENGTH = ESP_NOW_ETH_ALEN;

        static constexpr auto PREFERENCES_NAME = "esp-now";
        static constexpr auto PREFERENCES_KEY = "controller";

        std::array<uint8_t, MAC_LENGTH> controllerAddress = {};

    public:
        void begin() const
        {
            restore();
        }

        void send(const Message::Type type) const
        {
            const Message message{type};
            espNowSend(message);
        }

        [[nodiscard]] std::array<uint8_t, MAC_LENGTH> getControllerAddress() const
        {
            std::lock_guard lock(getMutex());
            return controllerAddress;
        }

        void setControllerAddress(const std::array<uint8_t, MAC_LENGTH>& address)
        {
            espNowAddPeer(address);
            persist(address);
            {
                std::lock_guard lock(getMutex());
                controllerAddress = address;
            }
        }

        [[nodiscard]] bool hasControllerAddress() const
        {
            std::lock_guard lock(getMutex());
            bool hasControllerAddress = true;
            for (const auto& byte : controllerAddress)
            {
                hasControllerAddress = hasControllerAddress && byte != 0;
            }
            return hasControllerAddress;
        }

    private:
        static std::mutex& getMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        static void persist(const std::array<uint8_t, MAC_LENGTH>& address)
        {
            if (Preferences prefs; prefs.begin(PREFERENCES_NAME, false))
            {
                prefs.putBytes(PREFERENCES_KEY, address.data(), address.size());
                prefs.end();
                ESP_LOGI(LOG_TAG, "Devices saved to Preferences");
            }
            else
            {
                ESP_LOGE(LOG_TAG, "Failed to open Preferences for saving");
            }
        }

        void restore() const
        {
            if (Preferences prefs; prefs.begin(PREFERENCES_NAME, true))
            {
                if (const auto dataSize = prefs.getBytesLength(PREFERENCES_KEY);
                    dataSize == MAC_LENGTH)
                {
                    std::lock_guard lock(getMutex());
                    prefs.getBytes(PREFERENCES_KEY, const_cast<uint8_t*>(controllerAddress.data()), dataSize);
                    ESP_LOGI(LOG_TAG, "Devices restored from Preferences");
                }
                prefs.end();
            }
            else
            {
                ESP_LOGE(LOG_TAG, "Failed to open Preferences for reading");
            }
        }

        static void espNowAddPeer(const std::array<uint8_t, MAC_LENGTH>& address)
        {
            if (!esp_now_is_peer_exist(address.data()))
            {
                esp_now_peer_info_t peerInfo = {
                    .peer_addr = {},
                    .lmk = {},
                    .channel = 0,
                    .ifidx = WIFI_IF_STA,
                    .encrypt = false,
                    .priv = nullptr,
                };
                std::copy_n(address.begin(), address.size(), peerInfo.peer_addr);
                if (esp_now_add_peer(&peerInfo) != ESP_OK)
                {
                    esp_now_deinit();
                    if (esp_now_init() == ESP_OK)
                    {
                        ESP_LOGW(LOG_TAG, "ESP-NOW reinitialized defensively");
                        esp_now_add_peer(&peerInfo);
                    }
                    else
                    {
                        ESP_LOGE(LOG_TAG, "Failed to recover ESP-NOW");
                    }
                }
                else
                {
                    ESP_LOGI(LOG_TAG, "Peer added successfully");
                }
            }
        }

        void espNowSend(const Message& message) const
        {
            std::lock_guard lock(getMutex());
            espNowAddPeer(controllerAddress);
            switch (esp_now_send(controllerAddress.data(), reinterpret_cast<const uint8_t*>(&message), sizeof(message)))
            {
            case ESP_ERR_ESPNOW_NOT_INIT:
                ESP_LOGE(LOG_TAG, "ESPNOW is not initialized");
                break;
            case ESP_ERR_ESPNOW_ARG:
                ESP_LOGE(LOG_TAG, "Invalid argument");
                break;
            case ESP_ERR_ESPNOW_INTERNAL:
                ESP_LOGE(LOG_TAG, "Internal error");
                break;
            case ESP_ERR_ESPNOW_NO_MEM:
                ESP_LOGE(LOG_TAG,
                         "Out of memory, when this happens, you can delay a while before sending the next data");
                break;
            case ESP_ERR_ESPNOW_NOT_FOUND:
                ESP_LOGE(LOG_TAG, "Peer is not found");
                break;
            case ESP_ERR_ESPNOW_IF:
                ESP_LOGE(LOG_TAG, "Current WiFi interface doesn't match that of peer");
                break;
            default:
                ESP_LOGI(LOG_TAG, "Message sent successfully");
                break;
            }
        }

        void fillState(const JsonObject& root) const override
        {
            const auto address = getControllerAddress();
            const auto espNow = root["espNow"].to<JsonObject>();
            char macString[18] = {};
            snprintf(macString, sizeof(macString), "%02X:%02X:%02X:%02X:%02X:%02X",
                     address[0], address[1], address[2], address[3], address[4], address[5]);
            espNow["controllerAddress"] = macString;
        }

        void clearServiceAndCharacteristics() override
        {
            ESP_LOGI(LOG_TAG, "No BLE pointers to be cleared");
        }

        void createServiceAndCharacteristics(NimBLEServer* server) override
        {
            const auto bleService = server->createService(BLE::UUID::ESP_NOW_REMOTE_SERVICE);
            bleService->createCharacteristic(
                BLE::UUID::ESP_NOW_CONTROLLER_CHARACTERISTIC,
                READ | WRITE
            )->setCallbacks(new EspNowControllerCallback(*this));
            bleService->start();
        }

        class EspNowControllerCallback final : public NimBLECharacteristicCallbacks
        {
            RemoteHandler& espNowHandler;

        public:
            explicit EspNowControllerCallback(RemoteHandler& espNowHandler)
                : espNowHandler(espNowHandler)
            {
            }

            void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
            {
                std::array<uint8_t, MAC_LENGTH> controllerAddress = {};
                const auto value = pCharacteristic->getValue();
                std::copy_n(value.begin(), value.size(), controllerAddress.begin());
                espNowHandler.setControllerAddress(controllerAddress);
            }

            void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
            {
                const auto address = espNowHandler.getControllerAddress();
                pCharacteristic->setValue(address.data(), address.size());
            }
        };
    };
}
