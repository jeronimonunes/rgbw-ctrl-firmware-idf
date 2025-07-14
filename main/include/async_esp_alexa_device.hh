#pragma once

#include <ArduinoJson.h>
#include <WiFi.h>
#include <array>
#include <utility>

/**
 * @brief Base class for Alexa-compatible smart lighting devices.
 *
 * This class defines the common interface and behavior for devices such as on/off lights,
 * dimmable lights, white spectrum, and color lights that follow the Hue-compatible Alexa API.
 *
 * The friend class externally manages device state updates AsyncEspAlexaWebHandler,
 * which invokes handleStateUpdate() upon receiving Alexa directives (e.g., PUT requests).
 *
 * Note: Direct setters like setOn(), setBrightness() do NOT trigger callbacks automatically.
 *       This is intentional and allows for internal updates or silent initialization.
 *       To notify the system or hardware, the handler must call callAfterStateUpdateCallback().
 */
class AsyncEspAlexaDevice
{
public:
    static constexpr auto MAX_DEVICE_NAME_LENGTH = 32;

private:
    friend class AsyncEspAlexaManager;
    friend class AsyncEspAlexaWebHandler;

    uint8_t id;
    const String name;
    std::function<void(AsyncEspAlexaDevice* device)> beforeStateUpdateCallback = nullptr;

    mutable std::array<char, 27> uniqueIdCache = {};
    bool on;

    virtual void callAfterStateUpdateCallback() = 0;

    virtual void callBeforeStateUpdateCallback()
    {
        if (beforeStateUpdateCallback)
            beforeStateUpdateCallback(this);
    }


    void setId(const uint8_t id)
    {
        this->id = id;
        uniqueIdCache = {};
    }

    [[nodiscard]] const char* getUniqueId() const
    {
        if (uniqueIdCache[0] == '\0')
        {
            uint8_t mac[6];
            WiFi.macAddress(mac);
            snprintf(uniqueIdCache.data(), uniqueIdCache.size(),
                     "%02X:%02X:%02X:%02X:%02X:%02X-%02X-00:11",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], this->id);
        }
        return uniqueIdCache.data();
    }

    static uint32_t getMac24()
    {
        String escapedMac = WiFi.macAddress();
        escapedMac.replace(":", "");
        escapedMac.toLowerCase();
        const String macSubStr = escapedMac.substring(6, 12);
        return strtol(macSubStr.c_str(), nullptr, 16);
    }

    [[nodiscard]] static uint32_t encodeLightKey(const uint8_t idx)
    {
        static uint32_t mac24 = getMac24();
        return mac24 << 7 | idx;
    }

    [[nodiscard]] static uint8_t decodeLightKey(const uint32_t key)
    {
        static uint32_t mac24 = getMac24();
        static constexpr uint8_t INVALID_DEVICE_INDEX = 255U;
        return key >> 7 == mac24 ? key & 127U : INVALID_DEVICE_INDEX;
    }

protected:
    virtual void handleStateUpdate(const JsonObject& obj)
    {
        if (obj["on"].is<bool>())
            setOn(obj["on"]);
    }

public:
    explicit AsyncEspAlexaDevice(String name, const bool on = false)
        : id(0), name(std::move(name)), on(on)
    {
    }

    virtual ~AsyncEspAlexaDevice() = default;

    void setBeforeStateUpdateCallback(const std::function<void(AsyncEspAlexaDevice* device)>& callback)
    {
        this->beforeStateUpdateCallback = callback;
    }

    virtual const char* getType() = 0;
    virtual const char* getModelId() = 0;
    virtual const char* getProductName() = 0;

    virtual void toJson(const JsonObject& obj)
    {
        obj["type"] = this->getType();
        obj["name"] = this->getName();
        obj["modelid"] = this->getModelId();
        obj["manufacturername"] = "Philips";
        obj["productname"] = this->getProductName();
        obj["uniqueid"] = getUniqueId();
        obj["swversion"] = "jeronimonunes-1.0.0";

        const auto state = obj["state"].to<JsonObject>();
        state["on"] = this->isOn();
        state["alert"] = "none";
        state["reachable"] = true;
    }

    [[nodiscard]] uint8_t getId() const
    {
        return id;
    }

    [[nodiscard]] String getName() const
    {
        return name;
    }

    [[nodiscard]] bool isOn() const
    {
        return on;
    }

    void setOn(const bool on)
    {
        this->on = on;
    }
};

class AsyncEspAlexaOnOffDevice : public AsyncEspAlexaDevice
{
    std::function<void(bool on)> onOffCallback = nullptr;

protected:
    void callAfterStateUpdateCallback() override
    {
        if (onOffCallback)
            onOffCallback(this->isOn());
    }

public:
    explicit AsyncEspAlexaOnOffDevice(const String& name,
                                      const bool on = false)
        : AsyncEspAlexaDevice(name, on)
    {
    }

    void setOnOffCallback(const std::function<void(bool on)>& callback)
    {
        this->onOffCallback = callback;
    }

    const char* getType() override
    {
        return "On/Off light";
    }

    const char* getModelId() override
    {
        return "HASS321";
    }

    const char* getProductName() override
    {
        return "E0";
    }
};

class AsyncEspAlexaDimmableDevice : public AsyncEspAlexaOnOffDevice
{
    uint8_t brightness;

    std::function<void(bool on, uint8_t brightness)> brightnessCallback = nullptr;

protected:
    void callAfterStateUpdateCallback() override
    {
        AsyncEspAlexaOnOffDevice::callAfterStateUpdateCallback();
        if (brightnessCallback)
            brightnessCallback(this->isOn(), this->getBrightness());
    }

    void handleStateUpdate(const JsonObject& obj) override
    {
        AsyncEspAlexaOnOffDevice::handleStateUpdate(obj);
        if (obj["bri"].is<JsonInteger>())
        {
            this->setBrightness(obj["bri"]);
        }
        else if (this->isOn())
        {
            this->setBrightness(254);
        }
    }

public:
    explicit AsyncEspAlexaDimmableDevice(const String& name,
                                         const bool on = false, const uint8_t brightness = 0)
        : AsyncEspAlexaOnOffDevice(name, on), brightness(brightness)
    {
    }

    void setBrightnessCallback(const std::function<void(bool on, uint8_t brightness)>& callback)
    {
        this->brightnessCallback = callback;
    }

    const char* getType() override
    {
        return "Dimmable light";
    }

    const char* getModelId() override
    {
        return "LWB010";
    }

    const char* getProductName() override
    {
        return "E1";
    }

    void toJson(const JsonObject& obj) override
    {
        AsyncEspAlexaOnOffDevice::toJson(obj);
        const auto state = obj["state"].as<JsonObject>();
        state["mode"] = "homeautomation";
        state["bri"] = this->getBrightness();
    }

    [[nodiscard]] uint8_t getBrightness() const
    {
        return brightness;
    }

    void setBrightness(const uint8_t brightness)
    {
        this->brightness = brightness;
    }
};

class AsyncEspAlexaWhiteSpectrumDevice final : public AsyncEspAlexaDimmableDevice
{
    uint16_t colorTemperature;

    std::function<void(bool on, uint8_t brightness, uint16_t colorTemperature)> callback = nullptr;

protected:
    void callAfterStateUpdateCallback() override
    {
        AsyncEspAlexaDimmableDevice::callAfterStateUpdateCallback();
        if (callback)
            callback(this->isOn(), this->getBrightness(), this->getColorTemperature());
    }

    void handleStateUpdate(const JsonObject& obj) override
    {
        AsyncEspAlexaDimmableDevice::handleStateUpdate(obj);
        if (obj["ct"].is<JsonInteger>())
            setColorTemperature(obj["ct"]);
    }

public:
    explicit AsyncEspAlexaWhiteSpectrumDevice(const String& name, const bool on = false,
                                              const uint8_t brightness = 0, const uint16_t colorTemperature = 500)
        : AsyncEspAlexaDimmableDevice(name, on, brightness), colorTemperature(colorTemperature)
    {
    }

    void setCallback(const std::function<void(bool on, uint8_t brightness, uint16_t colorTemperature)>& callback)
    {
        this->callback = callback;
    }

    const char* getType() override
    {
        return "Color temperature light";
    }

    const char* getModelId() override
    {
        return "LWT010";
    }

    const char* getProductName() override
    {
        return "E2";
    }

    void toJson(const JsonObject& obj) override
    {
        AsyncEspAlexaDimmableDevice::toJson(obj);
        const auto state = obj["state"].as<JsonObject>();
        state["colormode"] = "ct";
        state["ct"] = this->colorTemperature;
    }

    [[nodiscard]] uint16_t getColorTemperature() const
    {
        return colorTemperature;
    }

    void setColorTemperature(const uint16_t colorTemperature)
    {
        this->colorTemperature = colorTemperature;
    }
};

class AsyncEspAlexaColorDevice final : public AsyncEspAlexaDimmableDevice
{
    uint16_t hue;
    uint8_t saturation;

    std::function<void(bool on, uint8_t brightness, uint16_t hue, uint8_t saturation)> colorCallback = nullptr;

protected:
    void callAfterStateUpdateCallback() override
    {
        AsyncEspAlexaDimmableDevice::callAfterStateUpdateCallback();
        if (colorCallback)
            colorCallback(this->isOn(), this->getBrightness(), this->getHue(), this->getSaturation());
    }

    void handleStateUpdate(const JsonObject& obj) override
    {
        AsyncEspAlexaDimmableDevice::handleStateUpdate(obj);
        if (obj["hue"].is<JsonInteger>() && obj["sat"].is<JsonInteger>())
        {
            setColor(obj["hue"], obj["sat"]);
        }
    }

public:
    explicit AsyncEspAlexaColorDevice(const String& name, const bool on = false, const uint8_t brightness = 0,
                                      const uint16_t hue = 0, const uint8_t saturation = 0)
        : AsyncEspAlexaDimmableDevice(name, on, brightness), hue(hue), saturation(saturation)
    {
    }

    void setColorCallback(
        const std::function<void(bool on, uint8_t brightness, uint16_t hue, uint8_t saturation)>& callback)
    {
        this->colorCallback = callback;
    }

    const char* getType() override
    {
        return "Color light";
    }

    const char* getModelId() override
    {
        return "LST001";
    }

    const char* getProductName() override
    {
        return "E3";
    }

    void toJson(const JsonObject& obj) override
    {
        AsyncEspAlexaDimmableDevice::toJson(obj);
        const auto state = obj["state"].as<JsonObject>();
        state["colormode"] = "hs";
        state["hue"] = this->getHue();
        state["sat"] = this->getSaturation();
        state["effect"] = "none";
    }

    [[nodiscard]] uint16_t getHue() const
    {
        return hue;
    }

    [[nodiscard]] uint8_t getSaturation() const
    {
        return saturation;
    }

    void setHue(const uint16_t hue)
    {
        this->hue = hue;
    }

    void setSaturation(const uint8_t saturation)
    {
        this->saturation = saturation;
    }

    void setColor(const uint16_t hue, const uint8_t saturation)
    {
        this->hue = hue;
        this->saturation = saturation;
    }
};

class AsyncEspAlexaExtendedColorDevice final : public AsyncEspAlexaDimmableDevice
{
public:
    enum class ColorMode : uint8_t { ct = 0, hs = 1 };

private:
    uint16_t hue;
    uint8_t saturation;
    uint16_t colorTemperature;
    ColorMode mode;

    std::function<void(bool on, uint8_t brightness, uint16_t colorTemperature)> colorTemperatureCallback = nullptr;
    std::function<void(bool on, uint8_t brightness, uint16_t hue, uint8_t saturation)> colorCallback = nullptr;

protected:
    void callAfterStateUpdateCallback() override
    {
        AsyncEspAlexaDimmableDevice::callAfterStateUpdateCallback();
        if (ColorMode::ct == this->getColorMode() && colorTemperatureCallback)
            colorTemperatureCallback(this->isOn(), this->getBrightness(), this->getColorTemperature());
        if (ColorMode::hs == this->getColorMode() && colorCallback)
            colorCallback(this->isOn(), this->getBrightness(), this->getHue(), this->getSaturation());
    }

    void handleStateUpdate(const JsonObject& obj) override
    {
        AsyncEspAlexaDimmableDevice::handleStateUpdate(obj);
        if (obj["ct"].is<JsonInteger>())
        {
            setColorTemperature(obj["ct"]);
        }
        if (obj["hue"].is<JsonInteger>() && obj["sat"].is<JsonInteger>())
        {
            setColor(obj["hue"], obj["sat"]);
        }
    }

public:
    explicit AsyncEspAlexaExtendedColorDevice(const String& name, const bool on = false, const uint8_t brightness = 0,
                                              const uint16_t hue = 0, const uint8_t saturation = 0,
                                              const uint16_t colorTemperature = 500,
                                              const ColorMode mode = ColorMode::ct)
        : AsyncEspAlexaDimmableDevice(name, on, brightness), hue(hue), saturation(saturation),
          colorTemperature(colorTemperature), mode(mode)
    {
    }

    void setColorTemperatureCallback(
        const std::function<void(bool on, uint8_t brightness, uint16_t colorTemperature)>& callback)
    {
        this->colorTemperatureCallback = callback;
    }

    void setColorCallback(
        const std::function<void(bool on, uint8_t brightness, uint16_t hue, uint8_t saturation)>& callback)
    {
        this->colorCallback = callback;
    }

    const char* getType() override
    {
        return "Extended color light";
    }

    const char* getModelId() override
    {
        return "LCT015";
    }

    const char* getProductName() override
    {
        return "E4";
    }

    void toJson(const JsonObject& obj) override
    {
        AsyncEspAlexaDimmableDevice::toJson(obj);
        const auto state = obj["state"].as<JsonObject>();
        state["colormode"] = this->getColorModeString();
        state["ct"] = this->getColorTemperature();
        state["hue"] = this->getHue();
        state["sat"] = this->getSaturation();
        state["effect"] = "none";
    }

    [[nodiscard]] uint16_t getHue() const
    {
        return hue;
    }

    [[nodiscard]] uint8_t getSaturation() const
    {
        return saturation;
    }

    [[nodiscard]] uint16_t getColorTemperature() const
    {
        return colorTemperature;
    }

    [[nodiscard]] ColorMode getColorMode() const
    {
        return mode;
    }

    [[nodiscard]] const char* getColorModeString() const
    {
        return mode == ColorMode::ct ? "ct" : "hs";
    }

    void setHue(const uint16_t hue)
    {
        this->hue = hue;
    }

    void setSaturation(const uint8_t saturation)
    {
        this->saturation = saturation;
    }

    void setColorTemperature(const uint16_t colorTemperature)
    {
        this->colorTemperature = colorTemperature;
        this->mode = ColorMode::ct;
    }

    void setColor(const uint16_t hue, const uint8_t saturation)
    {
        this->hue = hue;
        this->saturation = saturation;
        this->mode = ColorMode::hs;
    }
};
