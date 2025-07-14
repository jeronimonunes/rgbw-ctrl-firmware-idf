#pragma once

#include "color.hh"
#include "light.hh"

#include <array>
#include <Arduino.h>
#include <algorithm>

#include "ble_service.hh"
#include "http_manager.hh"
#include "state_json_filler.hh"
#include "throttled_value.hh"

namespace Output
{
#pragma pack(push, 1)
    struct State
    {
        std::array<Light::State, 4> values = {};

        bool operator==(const State& other) const
        {
            return values == other.values;
        }

        bool operator!=(const State& other) const
        {
            return values != other.values;
        }

        [[nodiscard]] bool isOn(Color color) const
        {
            return values.at(static_cast<size_t>(color)).on;
        }

        [[nodiscard]] uint8_t getValue(Color color) const
        {
            return values.at(static_cast<size_t>(color)).value;
        }

        [[nodiscard]] bool anyOn() const
        {
            return std::any_of(values.begin(), values.end(),
                               [](const Light::State& s) { return s.on; });
        }
    };
#pragma pack(pop)

    class Manager final : public BLE::Service, public StateJsonFiller, public HTTP::AsyncWebHandlerCreator
    {
        static constexpr auto LOG_TAG = "Output";

        std::array<Light, 4> lights;
        static_assert(static_cast<size_t>(Color::White) < 4, "Color enum out of bounds");

        NimBLECharacteristic* bleOutputColorCharacteristic = nullptr;
        ThrottledValue<State> colorNotificationThrottle{500};

    public:
        explicit Manager(const gpio_num_t red,
                         const gpio_num_t green,
                         const gpio_num_t blue,
                         const gpio_num_t white)
            : lights{
                Light(red, false),
                Light(green, false),
                Light(blue, false),
                Light(white, false)
            }
        {
        }

        void begin()
        {
            for (auto& light : lights)
                light.setup();
        }

        void handle(const unsigned long now)
        {
            for (auto& light : lights)
                light.handle(now);
            sendColorNotification(now);
        }

        void setValue(const uint8_t value, Color color)
        {
            return lights.at(static_cast<size_t>(color)).setValue(value);
        }

        void setOn(const bool on, Color color)
        {
            return lights.at(static_cast<size_t>(color)).setOn(on);
        }

        void toggle(Color color)
        {
            auto& light = lights.at(static_cast<size_t>(color));
            const bool on = !light.isVisible();
            light.setOn(on);
            if (on)
                light.makeVisible();
        }

        void toggleAll()
        {
            const bool on = anyVisible();
            for (auto& light : lights)
            {
                if (on)
                {
                    light.setOn(false);
                }
                else
                {
                    light.setOn(true);
                    light.setValue(Light::ON_VALUE);
                }
            }
        }

        void turnOffAll()
        {
            for (auto& light : lights)
                light.setOn(false);
        }

        void turnOnAll()
        {
            for (auto& light : lights)
                light.makeVisible();
        }

        void increaseBrightness()
        {
            if (!anyOn())
            {
                for (auto& light : lights)
                {
                    light.setState({true, Light::OFF_VALUE});
                }
            }
            for (auto& light : lights)
                light.increaseBrightness();
        }

        void decreaseBrightness()
        {
            if (anyOn())
                for (auto& light : lights)
                    light.decreaseBrightness();
        }

        void setColor(const uint8_t r, const uint8_t g, const uint8_t b)
        {
            lights.at(static_cast<size_t>(Color::Red)).setValue(r);
            lights.at(static_cast<size_t>(Color::Green)).setValue(g);
            lights.at(static_cast<size_t>(Color::Blue)).setValue(b);
        }

        void setColor(const uint8_t r, const uint8_t g, const uint8_t b, const uint8_t w)
        {
            lights.at(static_cast<size_t>(Color::Red)).setValue(r);
            lights.at(static_cast<size_t>(Color::Green)).setValue(g);
            lights.at(static_cast<size_t>(Color::Blue)).setValue(b);
            lights.at(static_cast<size_t>(Color::White)).setValue(w);
        }

        void setOn(const bool r, const bool g, const bool b, const bool w)
        {
            lights.at(static_cast<size_t>(Color::Red)).setOn(r);
            lights.at(static_cast<size_t>(Color::Green)).setOn(g);
            lights.at(static_cast<size_t>(Color::Blue)).setOn(b);
            lights.at(static_cast<size_t>(Color::White)).setOn(w);
        }

        void setAll(const uint8_t value, const bool on)
        {
            for (auto& light : lights)
            {
                light.setValue(value);
                light.setOn(on);
            }
        }

        void setState(const State& state)
        {
            for (size_t i = 0; i < std::min(lights.size(), state.values.size()); ++i)
                lights.at(i).setState(state.values[i]);
        }

        [[nodiscard]] bool anyOn() const
        {
            return std::any_of(lights.begin(), lights.end(),
                               [](const Light& light) { return light.isOn(); });
        }

        [[nodiscard]] bool anyVisible() const
        {
            return std::any_of(lights.begin(), lights.end(),
                               [](const Light& light) { return light.isVisible(); });
        }

        [[nodiscard]] uint8_t getValue(Color color) const
        {
            return lights.at(static_cast<size_t>(color)).getValue();
        }

        [[nodiscard]] bool isOn(Color color) const
        {
            return lights.at(static_cast<size_t>(color)).isOn();
        }

        [[nodiscard]] std::array<uint8_t, 4> getValues() const
        {
            std::array<uint8_t, 4> output = {};
            std::transform(lights.begin(), lights.end(), output.begin(), [](const auto& light)
            {
                return light.getValue();
            });
            return output;
        }

        [[nodiscard]] State getState() const
        {
            std::array<Light::State, 4> state;
            std::transform(lights.begin(), lights.end(), state.begin(),
                           [](const Light& light) { return light.getState(); });
            return {state};
        }

        void fillState(const JsonObject& root) const override
        {
            const auto arr = root["output"].to<JsonArray>();
            for (const auto& light : lights)
                light.toJson(arr.add<JsonObject>());
        }

        AsyncWebHandler* createAsyncWebHandler() override
        {
            return new AsyncRestWebHandler(this);
        }

        void createServiceAndCharacteristics(NimBLEServer* server) override
        {
            ESP_LOGI(LOG_TAG, "Creating BLE services and characteristics");
            std::lock_guard bleLock(getBleMutex());
            const auto bleOutputService = server->createService(BLE::UUID::OUTPUT_SERVICE);
            bleOutputColorCharacteristic = bleOutputService->createCharacteristic(
                BLE::UUID::OUTPUT_COLOR_CHARACTERISTIC,
                READ | WRITE | NOTIFY
            );
            bleOutputColorCharacteristic->setCallbacks(new OutputColorCallback(this));
            bleOutputService->start();
            ESP_LOGI(LOG_TAG, "DONE creating BLE services and characteristics");
        }

        void clearServiceAndCharacteristics() override
        {
            ESP_LOGI(LOG_TAG, "Clearing all BLE saved pointers");
            std::lock_guard bleLock(getBleMutex());
            bleOutputColorCharacteristic = nullptr;
            ESP_LOGI(LOG_TAG, "DONE clearing all BLE saved pointers");
        }

        static std::mutex& getBleMutex()
        {
            static std::mutex bleMutex;
            return bleMutex;
        }

    private:
        void sendColorNotification(const unsigned long now)
        {
            std::lock_guard bleLock(getBleMutex());
            if (bleOutputColorCharacteristic == nullptr) return;

            State state = getState();
            if (!colorNotificationThrottle.shouldSend(now, state))
                return;

            bleOutputColorCharacteristic->setValue(reinterpret_cast<uint8_t*>(&state), sizeof(state));
            if (bleOutputColorCharacteristic->notify())
            {
                colorNotificationThrottle.setLastSent(now, state);
            }
        }

        class AsyncRestWebHandler final : public AsyncWebHandler
        {
            Manager* output;

        public:
            explicit AsyncRestWebHandler(Manager* output)
                : output(output)
            {
            }

            bool canHandle(AsyncWebServerRequest* request) const override
            {
                return request->method() == HTTP_GET &&
                (request->url() == HTTP::Endpoints::OUTPUT_BRIGHTNESS ||
                    request->url() == HTTP::Endpoints::OUTPUT_COLOR);
            }

            void handleRequest(AsyncWebServerRequest* request) override
            {
                if (request->url() == HTTP::Endpoints::OUTPUT_COLOR)
                {
                    return handleColorRequest(request);
                }
                return handleBrightnessRequest(request);
            }

            void handleBrightnessRequest(AsyncWebServerRequest* request) const
            {
                if (!request->hasParam("value"))
                    return sendMessageJsonResponse(request, "Missing 'value' parameter");
                if (const auto value = extractUint8Param(request, "value"))
                {
                    output->setState({true, value.value()});
                    sendMessageJsonResponse(request, "Brightness set");
                }
                else
                {
                    output->turnOffAll();
                    sendMessageJsonResponse(request, "Light turned off");
                }
            }

            void handleColorRequest(AsyncWebServerRequest* request) const
            {
                const auto oldR = output->getValue(Color::Red);
                const auto oldG = output->getValue(Color::Green);
                const auto oldB = output->getValue(Color::Blue);
                const auto oldW = output->getValue(Color::White);
                const auto r = extractUint8Param(request, "r");
                const auto g = extractUint8Param(request, "g");
                const auto b = extractUint8Param(request, "b");
                const auto w = extractUint8Param(request, "w");
                output->setColor(r.value_or(oldR), g.value_or(oldG), b.value_or(oldB), w.value_or(oldW));
                output->setOn(r.has_value(), g.has_value(), b.has_value(), w.has_value());
                sendMessageJsonResponse(request, "Color updated");
            }
        };

        class OutputColorCallback final : public NimBLECharacteristicCallbacks
        {
            Manager* output;

        public:
            explicit OutputColorCallback(Manager* output): output(output)
            {
            }

            void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
            {
                State state = {};
                constexpr uint8_t size = sizeof(State);
                if (pCharacteristic->getValue().size() != size)
                {
                    ESP_LOGE(LOG_TAG, "Received invalid Alexa color values length: %d",
                             pCharacteristic->getValue().size());
                    return;
                }
                memcpy(&state, pCharacteristic->getValue().data(), size);
                output->setState(state);
                output->colorNotificationThrottle.setLastSent(millis(), state);
            }

            void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
            {
                auto state = output->getState();
                pCharacteristic->setValue(reinterpret_cast<uint8_t*>(&state), sizeof(state));
            }
        };
    };
}
