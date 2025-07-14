#pragma once

#include "Arduino.h"
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include "async_esp_alexa_device.hh"
#include "async_esp_alexa_web_handler.hh"


class AsyncEspAlexaManager
{
    std::vector<AsyncEspAlexaDevice*> devices;
    bool discoverable = true;
    bool udpConnected = false;
    WiFiUDP espAlexaUdp;
    uint32_t mac24 = 0;
    String escapedMac = "";

    void respondToSearch()
    {
        IPAddress localIP = WiFi.localIP();
        char s[16];
        sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);
        char buf[1024];
        sprintf_P(buf,PSTR("HTTP/1.1 200 OK\r\n"
                      "EXT:\r\n"
                      "CACHE-CONTROL: max-age=100\r\n"
                      "LOCATION: http://%s:80/description.xml\r\n"
                      "SERVER: FreeRTOS/6.0.5, UPnP/1.0, IpBridge/1.17.0\r\n"
                      "hue-bridgeid: %s\r\n"
                      "ST: urn:schemas-upnp-org:device:basic:1\r\n"
                      "USN: uuid:2f402f80-da50-11e1-9b23-%s::upnp:rootdevice\r\n"
                      "\r\n"), s, escapedMac.c_str(), escapedMac.c_str());
        espAlexaUdp.beginPacket(espAlexaUdp.remoteIP(), espAlexaUdp.remotePort());
        espAlexaUdp.write(reinterpret_cast<uint8_t*>(buf), strlen(buf));
        espAlexaUdp.endPacket();
    }

public:
    bool begin()
    {
        escapedMac = WiFi.macAddress();
        escapedMac.replace(":", "");
        escapedMac.toLowerCase();
        const String macSubStr = escapedMac.substring(6, 12);
        mac24 = strtol(macSubStr.c_str(), nullptr, 16);
        udpConnected = espAlexaUdp.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
        if (udpConnected)
        {
            return true;
        }
        return false;
    }

    void loop()
    {
        if (!udpConnected) return;
        const int packetSize = espAlexaUdp.parsePacket();
        if (packetSize < 1) return;
        unsigned char packetBuffer[packetSize + 1];
        espAlexaUdp.read(packetBuffer, packetSize);
        packetBuffer[packetSize] = 0;
        espAlexaUdp.clear();
        if (!discoverable) return;
        const auto request = reinterpret_cast<const char*>(packetBuffer);
        if (strstr(request, "M-SEARCH") == nullptr) return;
        if (strstr(request, "ssdp:disc") != nullptr &&
            (strstr(request, "upnp:rootd") != nullptr ||
                strstr(request, "ssdp:all") != nullptr ||
                strstr(request, "asic:1") != nullptr))
        {
            respondToSearch();
        }
    }

    void reserve(const uint8_t size)
    {
        devices.reserve(size);
    }

    void addDevice(AsyncEspAlexaDevice* device)
    {
        device->setId(devices.size());
        devices.emplace_back(device);
    }

    void deleteAllDevices()
    {
        for (const auto& device : devices)
            delete device;
        devices.clear();
    }

    void setDiscoverable(const bool d)
    {
        discoverable = d;
    }

    [[nodiscard]] AsyncWebHandler* createAlexaAsyncWebHandler() const
    {
        return new AsyncEspAlexaWebHandler(devices);
    }
};
