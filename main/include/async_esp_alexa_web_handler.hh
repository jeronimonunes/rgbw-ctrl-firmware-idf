#pragma once

#include "Arduino.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include "async_esp_alexa_device.hh"

class AsyncEspAlexaWebHandler final : public AsyncWebHandler
{
    static constexpr auto LOG_TAG = "AsyncEspAlexaWebHandler";

    const std::vector<AsyncEspAlexaDevice*>& devices;
    String escapedMac;

public:
    explicit AsyncEspAlexaWebHandler(const std::vector<AsyncEspAlexaDevice*>& devices)
        : devices(devices)
    {
        escapedMac = WiFi.macAddress();
        escapedMac.replace(":", "");
        escapedMac.toLowerCase();
    }

    bool canHandle(AsyncWebServerRequest* request) const override
    {
        const auto& url = request->url();
        return url.startsWith("/description.xml") || url.startsWith("/api");
    }

    void handleBody(AsyncWebServerRequest* request,
                    uint8_t* data,
                    const size_t len,
                    const size_t index,
                    const size_t total) override
    {
        if (index == 0)
        {
            // this check allows request->_tempObject to be initialized from a middleware
            if (request->_tempObject == nullptr)
            {
                request->_tempObject = calloc(total + 1, sizeof(uint8_t)); // null-terminated string
                if (request->_tempObject == nullptr)
                {
                    ESP_LOGE(LOG_TAG, "Failed to allocate memory for request body");
                    request->abort();
                    return;
                }
            }
        }

        if (request->_tempObject != nullptr)
        {
            auto* buffer = static_cast<uint8_t*>(request->_tempObject);
            memcpy(buffer + index, data, len);
        }
    }

    void handleRequest(AsyncWebServerRequest* request) override
    {
        if (request->url() == "/description.xml")
            return serveDescription(request);
        handleAlexaApiCall(request);
    }

private:
    void serveDescription(AsyncWebServerRequest* request) const
    {
        IPAddress localIP = WiFi.localIP();
        char s[16];
        sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);
        char buf[1024];
        sprintf_P(buf,PSTR("<?xml version=\"1.0\" ?>"
                      "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
                      "<specVersion><major>1</major><minor>0</minor></specVersion>"
                      "<URLBase>http://%s:80/</URLBase>"
                      "<device>"
                      "<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
                      "<friendlyName>Espalexa (%s:80)</friendlyName>"
                      "<manufacturer>Royal Philips Electronics</manufacturer>"
                      "<manufacturerURL>http://www.philips.com</manufacturerURL>"
                      "<modelDescription>Philips hue Personal Wireless Lighting</modelDescription>"
                      "<modelName>Philips hue bridge 2012</modelName>"
                      "<modelNumber>929000226503</modelNumber>"
                      "<modelURL>http://www.meethue.com</modelURL>"
                      "<serialNumber>%s</serialNumber>"
                      "<UDN>uuid:2f402f80-da50-11e1-9b23-%s</UDN>"
                      "<presentationURL>index.html</presentationURL>"
                      "</device>"
                      "</root>"), s, s, escapedMac.c_str(), escapedMac.c_str());
        request->send(200, "text/xml", buf);
    }

    void handleAlexaApiCall(AsyncWebServerRequest* request) const
    {
        const auto& url = request->url();
        ESP_LOGD(LOG_TAG, "Received %s request: %s", request->methodToString(), url.c_str());

        if (request->_tempObject != nullptr)
            return handleRequestWithBody(request);

        if (url.indexOf("/state") > 0 && request->_tempObject == nullptr)
            return request->send(400, "application/json", R"({"error":"Empty or missing body"})");

        if (const int pos = url.indexOf("lights"); pos > 0)
            return handleLightsRequest(request, url, pos);

        request->send(404, "application/json", R"({"error":"Device not found"})");
    }

    void handleRequestWithBody(AsyncWebServerRequest* request) const
    {
        const String& url = request->url();
        ESP_LOGD(LOG_TAG, "Request body: %s", static_cast<char*>(request->_tempObject));

        JsonDocument doc;
        const auto error = deserializeJson(doc, request->_tempObject);
        free(request->_tempObject);
        request->_tempObject = nullptr;
        if (error)
        {
            ESP_LOGW(LOG_TAG, "JSON parse error: %s", error.c_str());
            request->send(400, "application/json", R"({"error":"Invalid JSON"})");
            return;
        }

        if (doc["devicetype"].is<JsonString>())
        {
            request->send(200, "application/json",
                          F("[{\"success\":{\"username\":\"2WLEDHardQrI3WHYTHoMcXHgEspsM8ZZRpSKtBQr\"}}]"));
            return;
        }

        if (url.indexOf("state") > 0)
        {
            const uint32_t devId = url.substring(url.indexOf("lights") + 7).toInt();
            const unsigned idx = AsyncEspAlexaDevice::decodeLightKey(devId);
            if (idx >= devices.size())
            {
                request->send(404, "application/json", R"({"error":"Device not found"})");
                return;
            }
            AsyncEspAlexaDevice* dev = devices[idx];
            if (!dev)
            {
                request->send(404, "application/json", R"({"error":"Device null"})");
                return;
            }
            dev->callBeforeStateUpdateCallback();
            dev->handleStateUpdate(doc.as<JsonObject>());
            dev->callAfterStateUpdateCallback();

            char buf[64];
            snprintf(buf, sizeof(buf), R"([{"success":{"/lights/%lu/state/": true}}])", devId);
            request->send(200, "application/json", buf);
        }
    }

    void handleLightsRequest(AsyncWebServerRequest* request, const String& url, const int pos) const
    {
        const int devId = url.substring(pos + 7).toInt();
        if (devId == 0) return handleListDeviceRequest(request);
        const auto idx = AsyncEspAlexaDevice::decodeLightKey(devId);
        return idx < devices.size()
                   ? handleGetDeviceStateRequest(request, idx)
                   : request->send(404, "application/json", R"({"error":"Device not found"})");
    }

    void handleListDeviceRequest(AsyncWebServerRequest* request) const
    {
        const auto response = new AsyncJsonResponse();
        const auto& obj = response->getRoot().as<JsonObject>();
        for (int i = 0; i < devices.size(); i++)
        {
            devices[i]->toJson(obj[String(AsyncEspAlexaDevice::encodeLightKey(i))].to<JsonObject>());
        }
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
        char buf[1024];
        serializeJson(response->getRoot(), buf, sizeof(buf));
        ESP_LOGD(LOG_TAG, "Sending response: %s", buf);
#endif
        response->setLength();
        request->send(response);
    }

    void handleGetDeviceStateRequest(AsyncWebServerRequest* request, const uint8_t idx) const
    {
        const auto response = new AsyncJsonResponse();
        devices[idx]->toJson(response->getRoot().to<JsonObject>());
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
        char buf[1024];
        serializeJson(response->getRoot(), buf, sizeof(buf));
        ESP_LOGD(LOG_TAG, "Sending response: %s", buf);
#endif
        response->setLength();
        request->send(response);
    }
};
