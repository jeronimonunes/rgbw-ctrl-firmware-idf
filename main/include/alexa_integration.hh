#pragma once

#include "ArduinoJson.h"
#include "async_esp_alexa_manager.hh"
#include "async_esp_alexa_color_utils.hh"

#include "output_manager.hh"

class AlexaIntegration final : public BLE::Service, public StateJsonFiller
{
    static constexpr auto LOG_TAG = "AlexaIntegration";
    static constexpr unsigned long OUTPUT_STATE_UPDATE_INTERVAL_MS = 500;

public:
#pragma pack(push, 1)
    struct Settings
    {
        static constexpr auto MAX_DEVICE_NAME_LENGTH
            = AsyncEspAlexaDevice::MAX_DEVICE_NAME_LENGTH;

        enum class Mode : uint8_t
        {
            OFF = 0,
            RGBW_DEVICE = 1,
            RGB_DEVICE = 2,
            MULTI_DEVICE = 3
        };

        Mode integrationMode = Mode::OFF;
        std::array<std::array<char, MAX_DEVICE_NAME_LENGTH>, 4> deviceNames = {};

        bool operator==(const Settings& other) const
        {
            return this->integrationMode == other.integrationMode
                && this->deviceNames == other.deviceNames;
        }

        bool operator!=(const Settings& other) const
        {
            return this->integrationMode != other.integrationMode
                || this->deviceNames != other.deviceNames;
        }

        void toJson(const JsonObject& to) const
        {
            to["mode"] = this->integrationModeString();
            const auto names = to["names"].to<JsonArray>();
            for (const auto& name : deviceNames)
                if (name[0] != '\0') names.add(name.data()); // NOLINT
        }

        [[nodiscard]] const char* integrationModeString() const
        {
            switch (integrationMode)
            {
            case Mode::OFF:
                return "off";
            case Mode::RGBW_DEVICE:
                return "rgbw_device";
            case Mode::RGB_DEVICE:
                return "rgb_device";
            case Mode::MULTI_DEVICE:
                return "multi_device";
            }
            return "off";
        }
    };

private:
    struct RgbwMode
    {
        AsyncEspAlexaExtendedColorDevice* device = nullptr;
    };

    struct RgbMode
    {
        AsyncEspAlexaColorDevice* rgbDevice = nullptr;
        AsyncEspAlexaDimmableDevice* standaloneDevice = nullptr;
    };

    struct MultiMode
    {
        std::array<AsyncEspAlexaDimmableDevice*, 4> devices = {nullptr, nullptr, nullptr, nullptr};
    };

    union ModeDevice
    {
        RgbwMode rgbw;
        RgbMode rgb;
        MultiMode multi;
    };
#pragma pack(pop)

    Output::Manager& outputManager;
    AsyncEspAlexaManager espAlexaManager;

    Settings settings;
    ModeDevice devices = {};
    Output::State outputState;
    unsigned long lastOutputStateUpdate = 0;

public:
    explicit AlexaIntegration(Output::Manager& output): outputManager(output)
    {
    }

    ~AlexaIntegration() override
    {
        clearDevices();
    }

    void begin()
    {
        loadPreferences();
        setupDevices();
        espAlexaManager.begin();
        outputState = outputManager.getState();
    }

    void handle(const unsigned long now)
    {
        espAlexaManager.loop();
        if (now - lastOutputStateUpdate >= OUTPUT_STATE_UPDATE_INTERVAL_MS)
        {
            lastOutputStateUpdate = now;
            if (const auto newOutputState = outputManager.getState();
                outputState != newOutputState)
            {
                outputState = newOutputState;
                updateDevices();
            }
        }
    }

    [[nodiscard]] AsyncWebHandler* createAsyncWebHandler() const
    {
        return espAlexaManager.createAlexaAsyncWebHandler();
    }

    [[nodiscard]] const Settings& getSettings() const
    {
        return settings;
    }

    void applySettings(const Settings& settings)
    {
        this->settings = settings;
        savePreferences();
        clearDevices();
        setupDevices();
    }

private:
    void clearDevices()
    {
        espAlexaManager.deleteAllDevices();
        devices = {};
    }

    void updateDevices() const
    {
        switch (settings.integrationMode)
        {
        case Settings::Mode::OFF:
            break;
        case Settings::Mode::RGBW_DEVICE:
            updateRgbwDevice();
            break;
        case Settings::Mode::RGB_DEVICE:
            updateRgbDevice();
            updateStandaloneDevice();
            break;
        case Settings::Mode::MULTI_DEVICE:
            updateMultiDevices();
            break;
        }
    }

    void loadPreferences()
    {
        Preferences prefs;
        prefs.begin("alexa-config", true);

        if (const auto mode = prefs.getUChar("mode", static_cast<uint8_t>(Settings::Mode::OFF));
            mode <= static_cast<uint8_t>(Settings::Mode::MULTI_DEVICE))
        {
            settings.integrationMode = static_cast<Settings::Mode>(mode);
        }
        else
        {
            settings.integrationMode = Settings::Mode::OFF;
        }

        const String r = prefs.getString("r", "");
        const String g = prefs.getString("g", "");
        const String b = prefs.getString("b", "");
        const String w = prefs.getString("w", "");
        prefs.end();

        strncpy(settings.deviceNames[0].data(), r.c_str(), Settings::MAX_DEVICE_NAME_LENGTH - 1);
        strncpy(settings.deviceNames[1].data(), g.c_str(), Settings::MAX_DEVICE_NAME_LENGTH - 1);
        strncpy(settings.deviceNames[2].data(), b.c_str(), Settings::MAX_DEVICE_NAME_LENGTH - 1);
        strncpy(settings.deviceNames[3].data(), w.c_str(), Settings::MAX_DEVICE_NAME_LENGTH - 1);

        settings.deviceNames[0][Settings::MAX_DEVICE_NAME_LENGTH - 1] = '\0';
        settings.deviceNames[1][Settings::MAX_DEVICE_NAME_LENGTH - 1] = '\0';
        settings.deviceNames[2][Settings::MAX_DEVICE_NAME_LENGTH - 1] = '\0';
        settings.deviceNames[3][Settings::MAX_DEVICE_NAME_LENGTH - 1] = '\0';
    }

    void savePreferences()
    {
        Preferences prefs;
        prefs.begin("alexa-config", false);
        prefs.putUChar("mode", static_cast<uint8_t>(settings.integrationMode));
        prefs.putString("r", settings.deviceNames[0].data());
        prefs.putString("g", settings.deviceNames[1].data());
        prefs.putString("b", settings.deviceNames[2].data());
        prefs.putString("w", settings.deviceNames[3].data());
        prefs.end();
    }

    void setupDevices()
    {
        switch (settings.integrationMode)
        {
        case Settings::Mode::OFF:
            break;
        case Settings::Mode::RGBW_DEVICE:
            espAlexaManager.reserve(1);
            setupRgbwDevice();
            break;
        case Settings::Mode::RGB_DEVICE:
            espAlexaManager.reserve(2);
            setupRgbDevice();
            setupStandaloneDevice();
            break;
        case Settings::Mode::MULTI_DEVICE:
            espAlexaManager.reserve(4);
            setupMultiDevice();
            break;
        }
    }

    void setupRgbwDevice()
    {
        ESP_LOGI(LOG_TAG, "Adding RGBW device: %s", settings.deviceNames[0].data());
        const char* deviceName = settings.deviceNames[0].data();
        const auto r = outputManager.getValue(Color::Red);
        const auto g = outputManager.getValue(Color::Green);
        const auto b = outputManager.getValue(Color::Blue);
        const auto w = outputManager.getValue(Color::White);
        const auto [h, s, v] = AsyncEspAlexaColorUtils::rgbwToHsv(r, g, b, w);
        const auto on = outputManager.anyOn();
        devices.rgbw.device = new AsyncEspAlexaExtendedColorDevice(
            deviceName, on, v, h, s, 500,
            AsyncEspAlexaExtendedColorDevice::ColorMode::hs);
        espAlexaManager.addDevice(devices.rgbw.device);

        devices.rgbw.device->setColorCallback([this](const bool isOn,
                                                     const uint8_t brightness,
                                                     const uint16_t hue,
                                                     const uint8_t saturation)
        {
            this->handleRgbwCommand(isOn, brightness, hue, saturation);
        });
        devices.rgbw.device->setColorTemperatureCallback([this](const bool isOn,
                                                                const uint8_t brightness,
                                                                const uint16_t colorTemperature)
        {
            this->handleRgbwCommand(isOn, brightness, colorTemperature);
        });
    }

    void setupRgbDevice()
    {
        if (settings.deviceNames[0][0] == '\0')
        {
            ESP_LOGW(LOG_TAG, "RGB device name is empty");
            return;
        }
        ESP_LOGI(LOG_TAG, "Adding RGB device: %s", settings.deviceNames[0].data());
        const auto [r, g, b, w]
            = outputManager.getValues();
        const auto on = outputManager.isOn(Color::Red)
            || outputManager.isOn(Color::Green)
            || outputManager.isOn(Color::Blue);
        const auto [h, s, v] =
            AsyncEspAlexaColorUtils::rgbToHsv(r, g, b);

        devices.rgb.rgbDevice = new AsyncEspAlexaColorDevice(
            settings.deviceNames[0].data(), on, v, h, s);
        espAlexaManager.addDevice(devices.rgb.rgbDevice);

        devices.rgb.rgbDevice->setColorCallback([this](const bool isOn, const uint8_t brightness,
                                                       const uint16_t hue, const uint8_t saturation)
        {
            this->handleRgbCommand(isOn, brightness, hue, saturation);
        });
    }

    void setupStandaloneDevice()
    {
        devices.rgb.standaloneDevice = createSingleChannelDevice(settings.deviceNames[3].data(), Color::White);
        if (devices.rgb.standaloneDevice)
            espAlexaManager.addDevice(devices.rgb.standaloneDevice);
    }

    void setupMultiDevice()
    {
        const auto rDevice = createSingleChannelDevice(settings.deviceNames[0].data(), Color::Red);
        const auto gDevice = createSingleChannelDevice(settings.deviceNames[1].data(), Color::Green);
        const auto bDevice = createSingleChannelDevice(settings.deviceNames[2].data(), Color::Blue);
        const auto wDevice = createSingleChannelDevice(settings.deviceNames[3].data(), Color::White);
        if (rDevice) espAlexaManager.addDevice(rDevice);
        if (gDevice) espAlexaManager.addDevice(gDevice);
        if (bDevice) espAlexaManager.addDevice(bDevice);
        if (wDevice) espAlexaManager.addDevice(wDevice);
        devices.multi = {rDevice, gDevice, bDevice, wDevice};
    }

    [[nodiscard]] AsyncEspAlexaDimmableDevice* createSingleChannelDevice(
        const char* name, Color color) const
    {
        if (name[0] == '\0')
        {
            ESP_LOGW(LOG_TAG, "Device name is empty");
            return nullptr;
        }
        ESP_LOGI(LOG_TAG, "Adding single device: %s", name);
        const auto value = outputManager.getValue(color);
        const auto on = outputManager.isOn(color);
        const auto device = new AsyncEspAlexaDimmableDevice(name, on, value);


        device->setBrightnessCallback([this, name, color](const bool isOn, const uint8_t brightness)
        {
            this->handleSingleChannelCommand(name, color, isOn, brightness);
        });
        return device;
    }

    void handleRgbwCommand(const bool isOn, const uint8_t brightness,
                           const uint16_t hue, const uint8_t saturation) const
    {
        ESP_LOGI(LOG_TAG, "Received HS command: on=%d, brightness=%u, hue=%u, saturation=%u",
                 isOn, brightness, hue, saturation);
        const auto [r, g, b,w]
            = AsyncEspAlexaColorUtils::hsvToRgbw(hue, saturation, brightness);
        ESP_LOGI(LOG_TAG, "Converted RGBW: r=%u, g=%u, b=%u, w=%u", r, g, b, w);
        outputManager.setColor(r, g, b, w);
        outputManager.setOn(isOn, Color::Red);
        outputManager.setOn(isOn, Color::Green);
        outputManager.setOn(isOn, Color::Blue);
        outputManager.setOn(isOn, Color::White);
    }

    void handleRgbwCommand(const bool isOn, const uint8_t brightness,
                           const uint16_t colorTemperature) const
    {
        ESP_LOGI(LOG_TAG, "Received CT command: on=%d, brightness=%u, colorTemperature=%u",
                 isOn, brightness, colorTemperature);
        const auto [r, g, b, w]
            = AsyncEspAlexaColorUtils::ctToRgbw(brightness, colorTemperature);
        ESP_LOGI(LOG_TAG, "Converted RGBW: r=%u, g=%u, b=%u, w=%u", r, g, b, w);
        outputManager.setColor(r, g, b, w);
        outputManager.setOn(isOn, Color::Red);
        outputManager.setOn(isOn, Color::Green);
        outputManager.setOn(isOn, Color::Blue);
        outputManager.setOn(isOn, Color::White);
    }

    void handleRgbCommand(const bool isOn, const uint8_t brightness,
                          const uint16_t hue, const uint8_t saturation) const
    {
        ESP_LOGI(LOG_TAG, "Received HS command: brightness=%u, hue=%u, saturation=%u",
                 brightness, hue, saturation);
        const auto [r, g, b] = AsyncEspAlexaColorUtils::hsvToRgb(hue, saturation, brightness);
        ESP_LOGI(LOG_TAG, "Converted RGB: r=%u, g=%u, b=%u", r, g, b);
        outputManager.setColor(r, g, b);
        outputManager.setOn(isOn, Color::Red);
        outputManager.setOn(isOn, Color::Green);
        outputManager.setOn(isOn, Color::Blue);
    }

    void handleSingleChannelCommand(__unused const char* name, const Color color,
                                    const bool isOn, const uint8_t brightness) const
    {
        ESP_LOGI(LOG_TAG, "Received %s command: on=%d, brightness=%u", name, isOn, brightness);
        outputManager.setOn(isOn, color);
        outputManager.setValue(brightness < 128 ? brightness : brightness + 1, color);
    }

    void updateRgbwDevice() const
    {
        if (!devices.rgbw.device) return;
        const auto r = outputState.getValue(Color::Red);
        const auto g = outputState.getValue(Color::Green);
        const auto b = outputState.getValue(Color::Blue);
        const auto w = outputState.getValue(Color::White);
        const auto [h, s, v] = AsyncEspAlexaColorUtils::rgbwToHsv(r, g, b, w);
        devices.rgbw.device->setOn(outputState.anyOn());
        devices.rgbw.device->setColor(h, s);
        devices.rgbw.device->setBrightness(v);
    }

    void updateRgbDevice() const
    {
        if (!devices.rgb.rgbDevice) return;
        const auto r = outputState.getValue(Color::Red);
        const auto g = outputState.getValue(Color::Green);
        const auto b = outputState.getValue(Color::Blue);

        const auto [h, s, v] = AsyncEspAlexaColorUtils::rgbToHsv(r, g, b);

        const auto on = outputState.isOn(Color::Red) || outputState.isOn(Color::Green) || outputState.isOn(Color::Blue);
        devices.rgb.rgbDevice->setOn(on);
        devices.rgb.rgbDevice->setColor(h, s);
        devices.rgb.rgbDevice->setBrightness(v);
    }

    void updateStandaloneDevice() const
    {
        updateDevice(devices.rgb.standaloneDevice, Color::White);
    }

    void updateMultiDevices() const
    {
        updateDevice(devices.multi.devices[0], Color::Red);
        updateDevice(devices.multi.devices[1], Color::Green);
        updateDevice(devices.multi.devices[2], Color::Blue);
        updateDevice(devices.multi.devices[3], Color::White);
    }

    void updateDevice(AsyncEspAlexaDimmableDevice* device, const Color color) const
    {
        if (!device) return;
        const auto brightness = std::clamp(outputState.getValue(color),
                                           AsyncEspAlexaColorUtils::ALEXA_MIN_BRI_VAL,
                                           AsyncEspAlexaColorUtils::ALEXA_MAX_BRI_VAL);

        const auto on = outputState.isOn(color);
        device->setOn(on);
        device->setBrightness(brightness);
    }

public:

    void fillState(const JsonObject& root) const override
    {
        getSettings().toJson(root["alexa"].to<JsonObject>());
    }

    void createServiceAndCharacteristics(NimBLEServer* server) override
    {
        const auto service = server->createService(BLE::UUID::ALEXA_SERVICE);
        service->createCharacteristic(
            BLE::UUID::ALEXA_SETTINGS_CHARACTERISTIC,
            READ | WRITE
        )->setCallbacks(new AlexaCallback(this));
        service->start();
    }

    void clearServiceAndCharacteristics() override
    {
        ESP_LOGI(LOG_TAG, "No BLE pointers to be cleared");
    }

private:
    class AlexaCallback final : public NimBLECharacteristicCallbacks
    {
        AlexaIntegration* alexaIntegration;

    public:
        explicit AlexaCallback(AlexaIntegration* alexaIntegration): alexaIntegration(alexaIntegration)
        {
        }

        void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            Settings settings;
            if (pCharacteristic->getValue().size() != sizeof(Settings))
            {
                ESP_LOGE(LOG_TAG, "Received invalid Alexa settings length: %d", pCharacteristic->getValue().size());
                return;
            }
            memcpy(&settings, pCharacteristic->getValue().data(), sizeof(Settings));
            alexaIntegration->applySettings(settings);
        }

        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            auto settings = alexaIntegration->getSettings();
            pCharacteristic->setValue(reinterpret_cast<uint8_t*>(&settings), sizeof(Settings));
        }
    };
};
