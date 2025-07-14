#pragma once

#include <array>
#include "websocket_message.hh"
#include "esp_now_handler_remote.hh"
#include "ble_manager.hh"
#include "throttled_value.hh"

namespace WebSocket
{
    class Handler final : public HTTP::AsyncWebHandlerCreator
    {
        static constexpr auto LOG_TAG = "WebSocketHandler";
        static constexpr auto HEAP_MESSAGE_INTERVAL_MS = 750;

        Output::Manager* outputManager;
        OTA::Handler* otaHandler;
        WiFiManager* wifiManager;
        HTTP::Manager* webServerHandler;
        AlexaIntegration* alexaIntegration;
        BLE::Manager* bleManager;
        DeviceManager* deviceManager;
        EspNow::ControllerHandler* controllerEspNowHandler;
        EspNow::RemoteHandler* remoteEspNowHandler;

        AsyncWebSocket ws = AsyncWebSocket("/ws");

        ThrottledValue<Output::State> outputThrottle{200};
        ThrottledValue<BLE::Status> bleStatusThrottle{200};
        ThrottledValue<std::array<char, DeviceManager::DEVICE_NAME_TOTAL_LENGTH>> deviceNameThrottle{200};
        ThrottledValue<OTA::State> otaStateThrottle{200};
        ThrottledValue<EspNow::DeviceData> espNowDevicesThrottle{200};
        ThrottledValue<std::array<uint8_t, 6>> espNowControllerThrottle{200};
        ThrottledValue<std::array<char, 10>> firmwareVersionThrottle{200};
        ThrottledValue<WiFiDetails> wifiDetailsThrottle{200};
        ThrottledValue<WiFiStatus> wifiStatusThrottle{200};
        ThrottledValue<AlexaIntegration::Settings> alexaSettingsThrottle{200};

        unsigned long lastSentHeapInfo = 0;

    public:
        Handler(
            Output::Manager* outputManager,
            OTA::Handler* otaHandler,
            WiFiManager* wifiManager,
            HTTP::Manager* webServerHandler,
            AlexaIntegration* alexaIntegration,
            BLE::Manager* bleManager,
            DeviceManager* deviceManager,
            EspNow::ControllerHandler* controllerEspNowHandler,
            EspNow::RemoteHandler* remoteEspNowHandler
        )
            :
            outputManager(outputManager),
            otaHandler(otaHandler),
            wifiManager(wifiManager),
            webServerHandler(webServerHandler),
            alexaIntegration(alexaIntegration),
            bleManager(bleManager),
            deviceManager(deviceManager),
            controllerEspNowHandler(controllerEspNowHandler),
            remoteEspNowHandler(remoteEspNowHandler)
        {
            ws.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client,
                              const AwsEventType type, void* arg, const uint8_t* data,
                              const size_t len)
            {
                this->handleWebSocketEvent(client, type, arg, data, len);
            });
        }

        void handle(const unsigned long now)
        {
            ws.cleanupClients();
            if (ws.count())
            {
                sendAllMessages(now);
            }
        }

        AsyncWebHandler* createAsyncWebHandler() override
        {
            return &ws;
        }

    private:
        // --------------------  Message Sending --------------------

        template <typename TState, typename TMessage, typename TThrottle>
        void sendThrottledMessage(const TState& state, TThrottle& throttle, const unsigned long now,
                                  AsyncWebSocketClient* client = nullptr)
        {
            if (!throttle.shouldSend(now, state) && !client)
                return;

            const TMessage message(state);
            const auto data = reinterpret_cast<const uint8_t*>(&message);
            constexpr size_t len = sizeof(TMessage);

            if (client)
            {
                client->binary(data, len);
            }
            else if (AsyncWebSocket::SendStatus::ENQUEUED == ws.binaryAll(data, len))
            {
                throttle.setLastSent(now, state);
            }
        }

        void sendAllMessages(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            sendHeapInfoMessage(now);
            sendOutputColorMessage(now, client);
            sendBleStatusMessage(now, client);
            sendDeviceNameMessage(now, client);
            sendOtaProgressMessage(now, client);
            sendEspNowDevicesMessage(now, client);
            sendEspNowControllerMessage(now, client);
            sendFirmwareVersionMessage(now, client);
            sendWiFiDetailsMessage(now, client);
            sendWiFiStatusMessage(now, client);
            sendAlexaIntegrationSettingsMessage(now, client);
        }

        void sendOutputColorMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            if (outputManager == nullptr) return;
            sendThrottledMessage<Output::State, ColorMessage>(
                outputManager->getState(), outputThrottle, now, client);
        }

        void sendBleStatusMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            if (bleManager == nullptr) return;
            sendThrottledMessage<BLE::Status, BleStatusMessage>(
                bleManager->getStatus(), bleStatusThrottle, now, client);
        }

        void sendDeviceNameMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            if (deviceManager == nullptr) return;
            const auto deviceName = deviceManager->getDeviceNameArray();
            sendThrottledMessage<std::array<char, DeviceManager::DEVICE_NAME_TOTAL_LENGTH>, DeviceNameMessage>(
                deviceName, deviceNameThrottle, now, client);
        }

        void sendOtaProgressMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            if (otaHandler == nullptr) return;
            sendThrottledMessage<OTA::State, OtaProgressMessage>(
                otaHandler->getState(), otaStateThrottle, now, client);
        }

        void sendHeapInfoMessage(const unsigned long now)
        {
            if (now - lastSentHeapInfo < HEAP_MESSAGE_INTERVAL_MS)
                return;
            lastSentHeapInfo = now;
            const auto freeHeap = esp_get_free_heap_size();
            const HeapMessage message(freeHeap);
            ws.binaryAll(reinterpret_cast<const uint8_t*>(&message), sizeof(HeapMessage));
        }

        void sendEspNowDevicesMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            if (controllerEspNowHandler == nullptr) return;
            sendThrottledMessage<EspNow::DeviceData, EspNowDevicesMessage>(
                controllerEspNowHandler->getDeviceData(), espNowDevicesThrottle, now, client);
        }

        void sendEspNowControllerMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            if (remoteEspNowHandler == nullptr) return;
            sendThrottledMessage<std::array<uint8_t, 6>, EspNowControllerMessage>(
                remoteEspNowHandler->getControllerAddress(), espNowControllerThrottle, now, client);
        }

        void sendFirmwareVersionMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            std::array<char, 10> version = {};
            std::strncpy(version.data(), DeviceManager::FIRMWARE_VERSION, version.size() - 1);
            version[version.size() - 1] = '\0';
            sendThrottledMessage<std::array<char, 10>, FirmwareVersionMessage>(
                version, firmwareVersionThrottle, now, client);
        }

        void sendWiFiDetailsMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            if (wifiManager == nullptr) return;
            sendThrottledMessage<WiFiDetails, WiFiDetailsMessage>(
                wifiManager->getWifiDetails(), wifiDetailsThrottle, now, client);
        }

        void sendWiFiStatusMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            if (wifiManager == nullptr) return;
            sendThrottledMessage<WiFiStatus, WiFiStatusMessage>(
                wifiManager->getStatus(), wifiStatusThrottle, now, client);
        }

        void sendAlexaIntegrationSettingsMessage(const unsigned long now, AsyncWebSocketClient* client = nullptr)
        {
            if (alexaIntegration == nullptr) return;
            sendThrottledMessage<AlexaIntegration::Settings, AlexaIntegrationSettingsMessage>(
                alexaIntegration->getSettings(), alexaSettingsThrottle, now, client);
        }

        // --------------------  Message Handling --------------------

        void handleWebSocketEvent(AsyncWebSocketClient* client,
                                  const AwsEventType type, void* arg, const uint8_t* data,
                                  const size_t len)
        {
            switch (type)
            {
            case WS_EVT_CONNECT:
                ESP_LOGD(LOG_TAG, "WebSocket client connected: %s", client->remoteIP().toString().c_str());
                sendAllMessages(millis(), client);
                break;
            case WS_EVT_DISCONNECT: // NOLINT
                ESP_LOGD(LOG_TAG, "WebSocket client disconnected: %s", client->remoteIP().toString().c_str());
                break;
            case WS_EVT_PONG:
                ESP_LOGD(LOG_TAG, "WebSocket pong received from client: %s", client->remoteIP().toString().c_str());
                break;
            case WS_EVT_ERROR:
                ESP_LOGE(LOG_TAG, "WebSocket error: %s", client->remoteIP().toString().c_str());
                break;
            case WS_EVT_DATA:
                this->handleWebSocketMessage(client, arg, data, len);
                break;
            default:
                break;
            }
        }

        void handleWebSocketMessage(
            AsyncWebSocketClient* client,
            void* arg,
            const uint8_t* data,
            const size_t len
        )
        {
            const auto info = static_cast<AwsFrameInfo*>(arg);
            if (info->opcode != WS_BINARY)
            {
                ESP_LOGD(LOG_TAG, "Received non-binary  Message, opcode: %d", info->opcode);
                return;
            }
            if (!info->final)
            {
                ESP_LOGD(LOG_TAG, "Received fragmented  Message, only final messages are processed");
                return;
            }
            if (info->index != 0)
            {
                ESP_LOGD(LOG_TAG, "Received fragmented  Message with index %lld, only index 0 is processed",
                         info->index);
                return;
            }
            if (info->len != len)
            {
                ESP_LOGD(LOG_TAG, "Received  Message with unexpected length: expected %lld, got %d", info->len,
                         len);
                return;
            }
            if (len < 1)
            {
                ESP_LOGD(LOG_TAG, "Received empty  Message");
                return;
            }

            const uint8_t messageTypeRaw = data[0];
            if (messageTypeRaw > static_cast<uint8_t>(Message::Type::ON_ALEXA_INTEGRATION_SETTINGS))
            {
                ESP_LOGD(LOG_TAG, "Received unknown  Message type: %d", messageTypeRaw);
                return;
            }

            const auto messageType = static_cast<Message::Type>(messageTypeRaw);
            ESP_LOGD(LOG_TAG, "Received  Message of type %d", static_cast<int>(messageType));

            this->handleWebSocketMessage(messageType, client, data, len);
        }

        void handleWebSocketMessage(
            const Message::Type messageType,
            AsyncWebSocketClient* client,
            const uint8_t* data,
            const size_t len
        )
        {
            switch (messageType)
            {
            case Message::Type::ON_COLOR:
                handleColorMessage(data, len);
                break;

            case Message::Type::ON_HTTP_CREDENTIALS:
                handleHttpCredentialsMessage(data, len);
                break;

            case Message::Type::ON_DEVICE_NAME:
                handleDeviceNameMessage(data, len);
                break;

            case Message::Type::ON_HEAP:
                ESP_LOGD(LOG_TAG, "Received HEAP message (ignored).");
                break;

            case Message::Type::ON_BLE_STATUS:
                handleBleStatusMessage(data, len);
                break;

            case Message::Type::ON_WIFI_CONNECTION_DETAILS:
                handleWiFiConnectionDetailsMessage(data, len);
                break;

            case Message::Type::ON_WIFI_SCAN_STATUS:
                handleOnWiFiScanStatus();
                break;

            case Message::Type::ON_WIFI_DETAILS: // NOLINT
                ESP_LOGD(LOG_TAG, "Received WIFI_DETAILS message (ignored).");
                break;

            case Message::Type::ON_OTA_PROGRESS:
                ESP_LOGD(LOG_TAG, "Received OTA_PROGRESS message (ignored).");
                break;

            case Message::Type::ON_ALEXA_INTEGRATION_SETTINGS:
                handleAlexaIntegrationSettingsMessage(data, len);
                break;

            default:
                client->text("Unknown message type");
                break;
            }
        }

        void handleColorMessage(const uint8_t* data, const size_t len)
        {
            if (outputManager == nullptr) return;
            if (len < sizeof(ColorMessage)) return;
            const auto* message = reinterpret_cast<const ColorMessage*>(data);
            outputThrottle.setLastSent(millis(), message->state);
            outputManager->setState(message->state);
        }

        void handleHttpCredentialsMessage(const uint8_t* data, const size_t len) const
        {
            if (webServerHandler == nullptr) return;
            if (len < sizeof(HttpCredentialsMessage)) return;
            const auto* message = reinterpret_cast<const HttpCredentialsMessage*>(data);
            webServerHandler->updateCredentials(message->credentials);
        }

        void handleDeviceNameMessage(const uint8_t* data, const size_t len) const
        {
            if (deviceManager == nullptr) return;
            if (len < sizeof(DeviceNameMessage)) return;
            const auto* message = reinterpret_cast<const DeviceNameMessage*>(data);
            deviceManager->setDeviceName(message->deviceName.data());
        }

        void handleBleStatusMessage(const uint8_t* data, const size_t len) const
        {
            if (bleManager == nullptr) return;
            if (len < sizeof(BleStatusMessage)) return;
            switch (
                const auto* message = reinterpret_cast<const BleStatusMessage*>(data);
                message->status
            )
            {
            case BLE::Status::ADVERTISING:
                async_call([this]
                {
                    bleManager->start();
                }, 4096, 0);
                break;
            case BLE::Status::OFF:
                async_call([this]
                {
                    bleManager->stop();
                }, 4096, 0);
                break;
            default:
                break;
            }
        }

        void handleWiFiConnectionDetailsMessage(const uint8_t* data, const size_t len) const
        {
            if (wifiManager == nullptr) return;
            if (len < sizeof(WiFiConnectionDetailsMessage)) return;
            const auto* message = reinterpret_cast<const WiFiConnectionDetailsMessage*>(data);
            wifiManager->connect(message->details);
        }

        void handleOnWiFiScanStatus() const
        {
            if (wifiManager == nullptr) return;
            wifiManager->triggerScan();
        }

        void handleAlexaIntegrationSettingsMessage(const uint8_t* data,
                                                   const size_t len) const
        {
            if (alexaIntegration == nullptr) return;
            if (len < sizeof(AlexaIntegrationSettingsMessage)) return;
            const auto* message = reinterpret_cast<const AlexaIntegrationSettingsMessage*>(data);
            alexaIntegration->applySettings(message->settings);
        }
    };
}
