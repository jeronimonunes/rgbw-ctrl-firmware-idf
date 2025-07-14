#include <Arduino.h>
#include <LittleFS.h>
#include <esp_now.h>

#include "wifi_manager.hh"
#include "board_led.hh"
#include "alexa_integration.hh"
#include "device_manager.hh"
#include "esp_now_handler_controller.hh"
#include "output_manager.hh"
#include "push_button.hh"
#include "ota_handler.hh"
#include "state_rest_handler.hh"
#include "rotary_encoder_manager.hh"
#include "websocket_handler.hh"
#include "esp_now_handler.hh"

#include "task_monitor.hpp"

void monitor_task(void *pvParameters)
{
    while (1)
    {
        print_task_stats();
        vTaskDelay(pdMS_TO_TICKS(5000)); // a cada 5s
    }
}

void beginAlexaAndWebServer();
void onDataReceived(const esp_now_recv_info_t* esp_now_info, const uint8_t* data, int data_len);

static constexpr auto LOG_TAG = "Controller";

BoardLED boardLED(ControllerHardware::Pin::BoardLed::RED,
                  ControllerHardware::Pin::BoardLed::GREEN,
                  ControllerHardware::Pin::BoardLed::BLUE);

PushButton boardButton(ControllerHardware::Pin::Button::BUTTON1);

Output::Manager outputManager(ControllerHardware::Pin::Output::RED,
                              ControllerHardware::Pin::Output::GREEN,
                              ControllerHardware::Pin::Output::BLUE,
                              ControllerHardware::Pin::Output::WHITE);

RotaryEncoderManager rotaryEncoderManager(ControllerHardware::Pin::Header::H1::P1,
                                          ControllerHardware::Pin::Header::H1::P2,
                                          ControllerHardware::Pin::Header::H1::P4);

WiFiManager wifiManager;
HTTP::Manager httpManager;
DeviceManager deviceManager;
EspNow::ControllerHandler espNowHandler;
AlexaIntegration alexaIntegration(outputManager);
OTA::Handler otaHandler(httpManager.getAuthenticationMiddleware());

std::array<uint8_t, 4> advertisementData =
    BLE::Manager::buildAdvertisementData(54321, 0xAA, 0xAA);

BLE::Manager bleManager(advertisementData,
                        deviceManager,
                        {
                            &deviceManager,
                            &wifiManager,
                            &httpManager,
                            &outputManager,
                            &espNowHandler,
                            &alexaIntegration
                        });

WebSocket::Handler webSocketHandler(&outputManager,
                                    &otaHandler,
                                    &wifiManager,
                                    &httpManager,
                                    &alexaIntegration,
                                    &bleManager,
                                    &deviceManager,
                                    &espNowHandler,
                                    nullptr);

StateRestHandler stateRestHandler({
    &deviceManager,
    &wifiManager,
    &bleManager,
    &outputManager,
    &otaHandler,
    &alexaIntegration,
    &espNowHandler
});

void setup()
{
    ESP_LOGI(LOG_TAG, "Starting controller");

    boardLED.begin();
    outputManager.begin();
    rotaryEncoderManager.begin();
    wifiManager.begin();
    deviceManager.begin();
    esp_now_init();
    esp_now_register_recv_cb(onDataReceived);
    espNowHandler.begin();
    wifiManager.setGotIpCallback(beginAlexaAndWebServer);
    boardButton.setLongPressCallback([] { bleManager.start(); });
    boardButton.setShortPressCallback([] { outputManager.toggleAll(); });
    rotaryEncoderManager.onTurnLeft([] { outputManager.increaseBrightness(); });
    rotaryEncoderManager.onTurnRight([] { outputManager.decreaseBrightness(); });

    LittleFS.begin(true);
    if (const auto credentials = WiFiManager::loadCredentials())
        wifiManager.connect(credentials.value());
    else
        bleManager.start();
    ESP_LOGI(LOG_TAG, "Startup complete");
    xTaskCreate(monitor_task, "monitor_task", 2048, NULL, 5, NULL);
}

void loop()
{
    const auto now = millis();

    bleManager.handle(now);
    boardButton.handle(now);
    deviceManager.handle(now);
    outputManager.handle(now);
    webSocketHandler.handle(now);
    alexaIntegration.handle(now);

    boardLED.handle(
        now,
        bleManager.getStatus(),
        wifiManager.getScanStatus(),
        wifiManager.getStatus(),
        otaHandler.getStatus() == OTA::Status::Started
    );
}

void beginAlexaAndWebServer()
{
    alexaIntegration.begin();
    httpManager.begin(
        alexaIntegration.createAsyncWebHandler(),
        {
            &webSocketHandler,
            &otaHandler,
            &stateRestHandler,
            &bleManager,
            &deviceManager,
            &outputManager
        }
    );
}

void onEspNowMessage(const EspNow::Message* message)
{
    switch (message->type)
    {
    case EspNow::Message::Type::ToggleRed:
        outputManager.toggle(Color::Red);
        break;
    case EspNow::Message::Type::ToggleGreen:
        outputManager.toggle(Color::Green);
        break;
    case EspNow::Message::Type::ToggleBlue:
        outputManager.toggle(Color::Blue);
        break;
    case EspNow::Message::Type::ToggleWhite:
        outputManager.toggle(Color::White);
        break;
    case EspNow::Message::Type::ToggleAll:
        outputManager.toggleAll();
        break;
    case EspNow::Message::Type::TurnOffAll:
        outputManager.turnOffAll();
        break;
    case EspNow::Message::Type::TurnOnAll:
        outputManager.turnOnAll();
        break;
    case EspNow::Message::Type::IncreaseBrightness:
        outputManager.increaseBrightness();
        break;
    case EspNow::Message::Type::DecreaseBrightness:
        outputManager.decreaseBrightness();
        break;
    }
}

void onDataReceived(const esp_now_recv_info_t* esp_now_info, const uint8_t* data, int data_len)
{
    const auto& mac = esp_now_info->src_addr;
    ESP_LOGI(LOG_TAG, "Data received from %02X:%02X:%02X:%02X:%02X:%02X, length: %d",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], data_len);

    if (!espNowHandler.isMacAllowed(mac))
    {
        ESP_LOGW(LOG_TAG, "MAC address not allowed, ignoring packet");
        return;
    }

    if (data_len != sizeof(EspNow::Message)) return;

    const auto message = reinterpret_cast<EspNow::Message*>(const_cast<uint8_t*>(data));

    onEspNowMessage(message);
}
