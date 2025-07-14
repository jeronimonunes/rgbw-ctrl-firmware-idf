#pragma once

#include <array>
#include <WiFi.h>

#include "ArduinoJson.h"

#define WIFI_MAX_SSID_LENGTH      32
#define WIFI_MAX_PASSWORD_LENGTH  64
#define WIFI_MAX_EAP_IDENTITY     128
#define WIFI_MAX_EAP_USERNAME     128
#define WIFI_MAX_EAP_PASSWORD     128
#define MAX_SCAN_NETWORK_COUNT    15

#pragma pack(push, 1)
enum class WifiScanEvent : uint8_t
{
    StartScan
};

enum class WifiScanStatus : uint8_t
{
    NOT_STARTED = 0,
    RUNNING = 1,
    COMPLETED = 2,
    FAILED = 3
};

enum class WiFiStatus : uint8_t
{
    DISCONNECTED = 0,
    CONNECTED = 1,
    CONNECTED_NO_IP = 2,
    WRONG_PASSWORD = 3,
    NO_AP_FOUND = 4,
    CONNECTION_FAILED = 5,
    UNKNOWN = 255
};

enum class WiFiEncryptionType : uint8_t
{
    OPEN = 0,
    WEP,
    WPA_PSK,
    WPA2_PSK,
    WPA_WPA2_PSK,
    ENTERPRISE,
    WPA2_ENTERPRISE = ENTERPRISE,
    WPA3_PSK,
    WPA2_WPA3_PSK,
    WAPI_PSK,
    WPA3_ENT_192,
    INVALID
};

enum class WiFiPhaseTwoType : uint8_t
{
    ESP_EAP_TTLS_PHASE2_EAP,
    ESP_EAP_TTLS_PHASE2_MSCHAPV2,
    ESP_EAP_TTLS_PHASE2_MSCHAP,
    ESP_EAP_TTLS_PHASE2_PAP,
    ESP_EAP_TTLS_PHASE2_CHAP
};

struct WiFiNetwork
{
    WiFiEncryptionType encryptionType = WiFiEncryptionType::INVALID;
    std::array<char, WIFI_MAX_SSID_LENGTH + 1> ssid = {};

    bool operator !=(const WiFiNetwork& other) const
    {
        return encryptionType != other.encryptionType || ssid != other.ssid;
    }

    bool operator ==(const WiFiNetwork& other) const
    {
        return !(*this != other);
    }
};

struct WiFiScanResult
{
    uint8_t resultCount = 0;
    WiFiNetwork networks[MAX_SCAN_NETWORK_COUNT] = {};

    bool operator!=(const WiFiScanResult& other) const
    {
        if (resultCount != other.resultCount)
            return true;

        for (uint8_t i = 0; i < resultCount; ++i)
        {
            if (networks[i] != other.networks[i])
                return true;
        }

        return false;
    }

    [[nodiscard]] bool contains(const String& ssid) const
    {
        for (uint8_t i = 0; i < resultCount; ++i)
        {
            if (networks[i].ssid[0] == '\0')
                continue;
            if (String(networks[i].ssid.data()) == ssid)
                return true;
        }
        return false;
    }
};

struct WiFiDetails
{
    std::array<char, WIFI_MAX_SSID_LENGTH + 1> ssid = {};
    std::array<uint8_t, 6> mac = {};
    uint32_t ip = 0;
    uint32_t gateway = 0;
    uint32_t subnet = 0;
    uint32_t dns = 0;

    bool operator==(const WiFiDetails& other) const
    {
        return ssid == other.ssid &&
            mac == other.mac &&
            ip == other.ip &&
            gateway == other.gateway &&
            subnet == other.subnet &&
            dns == other.dns;
    }

    bool operator!=(const WiFiDetails& other) const
    {
        return ssid != other.ssid ||
            mac != other.mac ||
            ip != other.ip ||
            gateway != other.gateway ||
            subnet != other.subnet ||
            dns != other.dns;
    }

    void setSsid(const String& newSsid)
    {
        std::memset(ssid.data(), 0, sizeof(ssid));
        strncpy(ssid.data(), newSsid.c_str(), WIFI_MAX_SSID_LENGTH);
        ssid[WIFI_MAX_SSID_LENGTH] = '\0';
    }

    static void toJson(const JsonObject& to)
    {
        to["ssid"] = WiFi.SSID();
        to["mac"] = WiFi.macAddress();
        to["ip"] = WiFi.localIP().toString();
        to["gateway"] = WiFi.gatewayIP().toString();
        to["subnet"] = WiFi.subnetMask().toString();
        to["dns"] = WiFi.dnsIP().toString();
    }
};

struct WiFiConnectionDetails
{
    struct SimpleWiFiConnectionCredentials
    {
        std::array<char, WIFI_MAX_PASSWORD_LENGTH + 1> password;
    };

    struct EAPWiFiConnectionCredentials
    {
        std::array<char, WIFI_MAX_EAP_IDENTITY + 1> identity = {};
        std::array<char, WIFI_MAX_EAP_USERNAME + 1> username = {};
        std::array<char, WIFI_MAX_EAP_PASSWORD + 1> password = {};
        WiFiPhaseTwoType phase2Type;
    };

    union WiFiConnectionDetailsCredentials
    {
        SimpleWiFiConnectionCredentials simple;
        EAPWiFiConnectionCredentials eap;
    };

    WiFiEncryptionType encryptionType = WiFiEncryptionType::INVALID;
    std::array<char, WIFI_MAX_SSID_LENGTH + 1> ssid = {};
    WiFiConnectionDetailsCredentials credentials = {};
};

#pragma pack(pop)
