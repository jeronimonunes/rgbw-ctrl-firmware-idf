#pragma once

#include <array>
#include <WiFi.h>
#include <esp_eap_client.h>
#include <Preferences.h>
#include <cstring>
#include <atomic>
#include <mutex>

#include "ble_manager.hh"
#include "state_json_filler.hh"
#include "NimBLEServer.h"
#include "NimBLEService.h"
#include "NimBLECharacteristic.h"
#include "wifi_model.hh"


class WiFiManager final : public BLE::Service, public StateJsonFiller
{
    static constexpr auto LOG_TAG = "WiFiManager";
    static constexpr auto PREFERENCES_NAME = "wifi-config";

    std::atomic<WiFiStatus> wifiStatus = WiFiStatus::DISCONNECTED;
    std::atomic<WifiScanStatus> scanStatus = WifiScanStatus::COMPLETED;
    WiFiDetails wifiDetails = {};

    QueueHandle_t wifiScanQueue = nullptr;
    WiFiScanResult scanResult;

    NimBLECharacteristic* bleDetailsCharacteristic = nullptr;
    NimBLECharacteristic* bleStatusCharacteristic = nullptr;
    NimBLECharacteristic* bleScanStatusCharacteristic = nullptr;
    NimBLECharacteristic* bleScanResultCharacteristic = nullptr;

    std::function<void()> gotIpChanged;

public:
    void begin()
    {
        WiFi.persistent(false);
        WiFi.mode(WIFI_MODE_STA); // NOLINT
        fillWiFiDetails();
        WiFi.onEvent([this](const WiFiEvent_t event, const WiFiEventInfo_t& info)
        {
            switch (event)
            {
            case ARDUINO_EVENT_WIFI_STA_CONNECTED:
                setStatus(WiFiStatus::CONNECTED_NO_IP);
                ESP_LOGI(LOG_TAG, "Connected to AP"); // NOLINT
                break;

            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                ESP_LOGI(LOG_TAG, "Got IP: %s", WiFi.localIP().toString().c_str()); // NOLINT
                setStatus(WiFiStatus::CONNECTED);
                if (gotIpChanged) gotIpChanged();
                break;

            case ARDUINO_EVENT_WIFI_STA_LOST_IP:
                ESP_LOGW(LOG_TAG, "Lost IP address"); // NOLINT
                setStatus(WiFiStatus::CONNECTED_NO_IP);
                break;

            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                ESP_LOGW(LOG_TAG, "Disconnected from AP. Reason: %d", info.wifi_sta_disconnected.reason); // NOLINT
                switch (info.wifi_sta_disconnected.reason)
                {
                case WIFI_REASON_AUTH_FAIL:
                    setStatus(WiFiStatus::WRONG_PASSWORD);
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                    setStatus(WiFiStatus::NO_AP_FOUND);
                    break;
                default:
                    setStatus(WiFiStatus::DISCONNECTED);
                    break;
                }
                break;
            default:
                ESP_LOGD(LOG_TAG, "Unhandled WiFi event: %d", event);
                break;
            }
        });
        startTasks();
    }

    bool triggerScan() // NOLINT
    {
        constexpr auto event = WifiScanEvent::StartScan;
        if (const BaseType_t success = xQueueSend(wifiScanQueue, &event, 0); success == errQUEUE_FULL)
        {
            ESP_LOGW(LOG_TAG, "Scan request ignored: already in progress or queued");
            return false;
        }
        return true;
    }

    [[nodiscard]] WiFiDetails getWifiDetails() const
    {
        return wifiDetails;
    }

    [[nodiscard]] WiFiStatus getStatus() const
    {
        return wifiStatus;
    }

    [[nodiscard]] static const char* wifiStatusString(const WiFiStatus wifiStatus)
    {
        switch (wifiStatus)
        {
        case WiFiStatus::DISCONNECTED:
            return "DISCONNECTED";
        case WiFiStatus::CONNECTED:
            return "CONNECTED";
        case WiFiStatus::CONNECTED_NO_IP:
            return "CONNECTED_NO_IP";
        case WiFiStatus::WRONG_PASSWORD:
            return "WRONG_PASSWORD";
        case WiFiStatus::NO_AP_FOUND:
            return "NO_AP_FOUND";
        case WiFiStatus::CONNECTION_FAILED:
            return "CONNECTION_FAILED";
        default:
            return "UNKNOWN";
        }
    }

    [[nodiscard]] WifiScanStatus getScanStatus() const
    {
        return scanStatus;
    }

    [[nodiscard]] const char* getScanStatusString() const
    {
        switch (scanStatus)
        {
        case WifiScanStatus::NOT_STARTED:
            return "NOT_STARTED";
        case WifiScanStatus::COMPLETED:
            return "COMPLETED";
        case WifiScanStatus::RUNNING:
            return "RUNNING";
        case WifiScanStatus::FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
        }
    }

    [[nodiscard]] WiFiScanResult getScanResult() const
    {
        std::lock_guard lock(getWiFiScanResultMutex());
        return scanResult;
    }

    void setGotIpCallback(std::function<void()> cb)
    {
        gotIpChanged = std::move(cb);
    }

    [[nodiscard]] static std::optional<WiFiConnectionDetails> loadCredentials()
    {
        WiFiConnectionDetails config = {};
        Preferences prefs;
        prefs.begin(PREFERENCES_NAME, true);
        config.encryptionType = static_cast<WiFiEncryptionType>(prefs.getUChar(
            "encryptionType", static_cast<uint8_t>(WiFiEncryptionType::INVALID)));
        if (config.encryptionType == WiFiEncryptionType::INVALID)
        {
            prefs.end();
            return std::nullopt; // No valid credentials found
        }
        if (isEap(config.encryptionType))
        {
            prefs.getBytes("identity", config.credentials.eap.identity.data(), WIFI_MAX_EAP_IDENTITY + 1);
            prefs.getBytes("username", config.credentials.eap.username.data(), WIFI_MAX_EAP_USERNAME + 1);
            prefs.getBytes("eapPassword", config.credentials.eap.password.data(), WIFI_MAX_EAP_PASSWORD + 1);
            config.credentials.eap.phase2Type = static_cast<WiFiPhaseTwoType>(prefs.getUChar(
                "phase2Type", static_cast<uint8_t>(WiFiPhaseTwoType::ESP_EAP_TTLS_PHASE2_EAP)));
            prefs.getBytes("ssid", config.ssid.data(), WIFI_MAX_SSID_LENGTH + 1);
        }
        else
        {
            prefs.getBytes("ssid", config.ssid.data(), WIFI_MAX_SSID_LENGTH + 1);
            prefs.getBytes("password", config.credentials.simple.password.data(), WIFI_MAX_PASSWORD_LENGTH + 1);
        }
        prefs.end();
        return config;
    }

    static void clearCredentials()
    {
        Preferences prefs;
        prefs.begin(PREFERENCES_NAME, false);

        prefs.remove("encryptionType");
        prefs.remove("ssid");
        prefs.remove("password");
        prefs.remove("identity");
        prefs.remove("username");
        prefs.remove("eapPassword");
        prefs.remove("phase2Type");

        prefs.end();
    }

    void connect(const WiFiConnectionDetails& details) // NOLINT
    {
        if (details.ssid[0] == '\0')
        {
            ESP_LOGE(LOG_TAG, "Cannot connect: SSID is empty");
            return;
        }
        saveCredentials(details);

        if (const int result = WiFi.scanComplete(); result == WIFI_SCAN_RUNNING || result >= 0)
            WiFi.scanDelete();

        WiFi.disconnect(true);

        if (isEap(details))
            connect(details.ssid.data(), details.credentials.eap);
        else
            connect(details.ssid.data(), details.credentials.simple);
    }

private:
    static bool isEap(const WiFiConnectionDetails& details)
    {
        return isEap(details.encryptionType);
    }

    static bool isEap(const WiFiEncryptionType& encryptionType)
    {
        switch (encryptionType)
        {
        case WiFiEncryptionType::WPA2_ENTERPRISE:
        case WiFiEncryptionType::WPA3_ENT_192:
            return true;
        default:
            return false;
        }
    }

    static void saveCredentials(const WiFiConnectionDetails& details)
    {
        Preferences prefs;
        prefs.begin(PREFERENCES_NAME, false);
        prefs.putUChar("encryptionType", static_cast<uint8_t>(details.encryptionType));
        if (isEap(details))
        {
            prefs.putBytes("identity", details.credentials.eap.identity.data(), WIFI_MAX_EAP_IDENTITY + 1);
            prefs.putBytes("username", details.credentials.eap.username.data(), WIFI_MAX_EAP_USERNAME + 1);
            prefs.putBytes("eapPassword", details.credentials.eap.password.data(), WIFI_MAX_EAP_PASSWORD + 1);
            prefs.putUChar("phase2Type", static_cast<uint8_t>(details.credentials.eap.phase2Type));
            prefs.putBytes("ssid", details.ssid.data(), WIFI_MAX_SSID_LENGTH + 1);
            prefs.remove("password");
        }
        else
        {
            prefs.putBytes("ssid", details.ssid.data(), WIFI_MAX_SSID_LENGTH + 1);
            prefs.putBytes("password", details.credentials.simple.password.data(), WIFI_MAX_PASSWORD_LENGTH + 1);
            prefs.remove("identity");
            prefs.remove("username");
            prefs.remove("eapPassword");
            prefs.remove("phase2Type");
        }
        prefs.end();
    }

    void setStatus(const WiFiStatus newStatus)
    {
        if (newStatus == wifiStatus) return;
        ESP_LOGI(LOG_TAG, "WiFi status changed: %s -> %s",
                 wifiStatusString(wifiStatus.load()), wifiStatusString(newStatus));
        wifiStatus = newStatus;
        fillWiFiDetails();

        std::lock_guard bleLock(getBleMutex());
        if (bleStatusCharacteristic)
        {
            ESP_LOGI(LOG_TAG, "Notifying WiFiStatus via BLE");
            bleStatusCharacteristic->setValue(reinterpret_cast<uint8_t*>(&wifiStatus),
                                              sizeof(wifiStatus));
            bleStatusCharacteristic->notify(); // NOLINT
            ESP_LOGI(LOG_TAG, "DONE notifying WiFiStatus via BLE");
        }
        if (bleDetailsCharacteristic)
        {
            ESP_LOGI(LOG_TAG, "Notifying WiFiDetails via BLE");
            bleDetailsCharacteristic->setValue(reinterpret_cast<uint8_t*>(&wifiDetails), sizeof(wifiDetails));
            bleDetailsCharacteristic->notify(); // NOLINT
            ESP_LOGI(LOG_TAG, "DONE notifying WiFiDetails via BLE");
        }
    }

    void fillWiFiDetails()
    {
        wifiDetails.setSsid(WiFi.SSID());
        WiFi.macAddress(wifiDetails.mac.data());
        wifiDetails.ip = static_cast<uint32_t>(WiFi.localIP());
        wifiDetails.gateway = static_cast<uint32_t>(WiFi.gatewayIP());
        wifiDetails.subnet = static_cast<uint32_t>(WiFi.subnetMask());
        wifiDetails.dns = static_cast<uint32_t>(WiFi.dnsIP());
    }

    void setScanStatus(const WifiScanStatus status)
    {
        if (status == scanStatus) return;
        this->scanStatus = status;
        std::lock_guard bleLock(getBleMutex());
        if (bleScanStatusCharacteristic)
        {
            bleScanStatusCharacteristic->setValue(reinterpret_cast<uint8_t*>(&scanStatus), sizeof(scanStatus));
            bleScanStatusCharacteristic->notify(); // NOLINT
        }
    }

    void setScanResult(const WiFiScanResult& r)
    {
        std::lock_guard ScanResultLock(getWiFiScanResultMutex());
        if (r != scanResult)
        {
            scanResult = r;
            std::lock_guard bleLock(getBleMutex());
            if (bleScanResultCharacteristic)
            {
                bleScanResultCharacteristic->setValue(reinterpret_cast<uint8_t*>(&scanResult), sizeof(scanResult));
                bleScanResultCharacteristic->notify(); // NOLINT
            }
        }
    }

    static std::mutex& getWiFiScanResultMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    static void connect(const char* ssid, const WiFiConnectionDetails::SimpleWiFiConnectionCredentials& details)
    {
        esp_wifi_sta_enterprise_disable();
        WiFi.begin(ssid, details.password[0] == '\0' ? nullptr : details.password.data());
    }

    static void connect(const char* ssid, const WiFiConnectionDetails::EAPWiFiConnectionCredentials& details)
    {
        esp_wifi_sta_enterprise_enable();
        esp_eap_client_set_identity(reinterpret_cast<const unsigned char *>(details.identity.data()),
                                    static_cast<int>(strlen(details.identity.data())));
        esp_eap_client_set_username(reinterpret_cast<const unsigned char*>(details.username.data()),
                                           static_cast<int>(strlen(details.username.data())));
        esp_eap_client_set_password(reinterpret_cast<const unsigned char*>(details.password.data()),
                                           static_cast<int>(strlen(details.password.data())));
        esp_eap_client_set_ttls_phase2_method(static_cast<esp_eap_ttls_phase2_types>(details.phase2Type));
        WiFi.begin(ssid);
    }

    void startTasks()
    {
        if (!wifiScanQueue)
        {
            wifiScanQueue = xQueueCreate(1, sizeof(WifiScanEvent));
            if (!wifiScanQueue)
            {
                ESP_LOGE(LOG_TAG, "Failed to create wifiScanQueue");
                return;
            }
            xTaskCreate(wifiScanNotifier, "WifiScanNotifier", 4096, this, 1, nullptr);
        }
    }

    static void wifiScanNotifier(void* param)
    {
        auto* manager = static_cast<WiFiManager*>(param);
        WifiScanEvent event;
        while (true) // NOLINT
        {
            if (xQueueReceive(manager->wifiScanQueue, &event, portMAX_DELAY) == pdTRUE
                && event == WifiScanEvent::StartScan)
            {
                manager->setScanStatus(WifiScanStatus::RUNNING);
                if (manager->getStatus() != WiFiStatus::CONNECTED)
                {
                    WiFi.disconnect();
                }
                WiFi.scanNetworks(true);
                while (WiFi.scanComplete() == WIFI_SCAN_RUNNING)
                {
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                const int16_t scanStatus = WiFi.scanComplete();
                if (scanStatus < 0)
                {
                    ESP_LOGE(LOG_TAG, "WiFi scan failed with status: %d", scanStatus);
                    manager->setScanStatus(WifiScanStatus::FAILED);
                    continue;
                }
                WiFiScanResult result = {};

                for (int i = 0; i < scanStatus && result.resultCount < MAX_SCAN_NETWORK_COUNT; ++i)
                {
                    auto ssid = WiFi.SSID(i);
                    if (ssid.isEmpty()) continue;

                    if (result.contains(ssid))
                        continue; // Skip duplicates

                    strncpy(result.networks[result.resultCount].ssid.data(), ssid.c_str(), WIFI_MAX_SSID_LENGTH);
                    result.networks[result.resultCount].ssid[WIFI_MAX_SSID_LENGTH] = '\0';
                    result.networks[result.resultCount].encryptionType =
                        static_cast<WiFiEncryptionType>(WiFi.encryptionType(i));
                    result.resultCount++;
                }
                WiFi.scanDelete();

                manager->setScanStatus(WifiScanStatus::COMPLETED);
                manager->setScanResult(result);
            }
        }
    } // NOLINT

public:
    void fillState(const JsonObject& obj) const override
    {
        const auto wifi = obj["wifi"].to<JsonObject>();
        WiFiDetails::toJson(wifi["details"].to<JsonObject>());
        wifi["status"] = wifiStatusString(wifiStatus);
    }

    void createServiceAndCharacteristics(NimBLEServer* server) override
    {
        std::lock_guard bleLock(getBleMutex());
        const auto bleService = server->createService(BLE::UUID::WIFI_SERVICE);

        bleDetailsCharacteristic = bleService->createCharacteristic(
            BLE::UUID::WIFI_DETAILS_CHARACTERISTIC,
            READ | NOTIFY
        );
        bleDetailsCharacteristic->setCallbacks(new WiFiDetailsCallback(this));

        bleStatusCharacteristic = bleService->createCharacteristic(
            BLE::UUID::WIFI_STATUS_CHARACTERISTIC,
            WRITE | READ | NOTIFY
        );
        bleStatusCharacteristic->setCallbacks(new WiFiStatusCallback(this));

        bleScanStatusCharacteristic = bleService->createCharacteristic(
            BLE::UUID::WIFI_SCAN_STATUS_CHARACTERISTIC,
            WRITE | READ | NOTIFY
        );
        bleScanStatusCharacteristic->setCallbacks(new WiFiScanStatusCallback(this));

        bleScanResultCharacteristic = bleService->createCharacteristic(
            BLE::UUID::WIFI_SCAN_RESULT_CHARACTERISTIC,
            READ | NOTIFY
        );
        bleScanResultCharacteristic->setCallbacks(new WiFiScanResultCallback(this));

        bleService->start();
    }

    void clearServiceAndCharacteristics() override
    {
        std::lock_guard bleLock(getBleMutex());
        bleDetailsCharacteristic = nullptr;
        bleStatusCharacteristic = nullptr;
        bleScanStatusCharacteristic = nullptr;
        bleScanResultCharacteristic = nullptr;
    }

    static std::mutex& getBleMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    class WiFiDetailsCallback final : public NimBLECharacteristicCallbacks
    {
        WiFiManager* wifiManager;

    public:
        explicit WiFiDetailsCallback(WiFiManager* wifiManager) : wifiManager(wifiManager)
        {
        }

        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            WiFiDetails details = wifiManager->getWifiDetails();
            pCharacteristic->setValue(reinterpret_cast<uint8_t*>(&details), sizeof(details));
        }
    };

    class WiFiStatusCallback final : public NimBLECharacteristicCallbacks
    {
        WiFiManager* wifiManager;

    public:
        explicit WiFiStatusCallback(WiFiManager* wifiManager) : wifiManager(wifiManager)
        {
        }

        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            WiFiStatus status = wifiManager->getStatus();
            pCharacteristic->setValue(reinterpret_cast<uint8_t*>(&status), sizeof(status));
        }

        void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            WiFiConnectionDetails details = {};
            if (pCharacteristic->getValue().size() != sizeof(WiFiConnectionDetails))
            {
                ESP_LOGE(LOG_TAG, "Received invalid WiFi connection details length: %d",
                         pCharacteristic->getValue().size());
                return;
            }
            memcpy(&details, pCharacteristic->getValue().data(), sizeof(WiFiConnectionDetails));
            wifiManager->connect(details);
        }
    };

    class WiFiScanStatusCallback final : public NimBLECharacteristicCallbacks
    {
        WiFiManager* wifiManager;

    public:
        explicit WiFiScanStatusCallback(WiFiManager* wifiManager) : wifiManager(wifiManager)
        {
        }

        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            WifiScanStatus scanStatus = wifiManager->getScanStatus();
            pCharacteristic->setValue(reinterpret_cast<uint8_t*>(&scanStatus), sizeof(scanStatus));
        }

        void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            wifiManager->triggerScan();
        }
    };

    class WiFiScanResultCallback final : public NimBLECharacteristicCallbacks
    {
        WiFiManager* wifiManager;

    public:
        explicit WiFiScanResultCallback(WiFiManager* wifiManager) : wifiManager(wifiManager)
        {
        }

        void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
        {
            WiFiScanResult scanResult = wifiManager->getScanResult();
            pCharacteristic->setValue(reinterpret_cast<uint8_t*>(&scanResult), sizeof(scanResult));
        }
    };
};
