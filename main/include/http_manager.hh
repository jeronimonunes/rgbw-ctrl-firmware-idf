#pragma once

#include <AsyncJson.h>

#include "ble_service.hh"

namespace HTTP
{
    namespace Endpoints
    {
        static constexpr auto STATE = "/state";
        static constexpr auto UPDATE = "/update";
        static constexpr auto BLUETOOTH = "/bluetooth";
        static constexpr auto SYSTEM_RESTART = "/system/restart";
        static constexpr auto SYSTEM_RESET = "/system/reset";
        static constexpr auto OUTPUT_COLOR = "/output/color";
        static constexpr auto OUTPUT_BRIGHTNESS = "/output/brightness";
    }

    class AsyncWebHandlerCreator
    {
    public:
        virtual ~AsyncWebHandlerCreator() = default;
        virtual AsyncWebHandler* createAsyncWebHandler() = 0;

        static void sendMessageJsonResponse(AsyncWebServerRequest* request, const char* message)
        {
            auto* response = new AsyncJsonResponse();
            response->getRoot().to<JsonObject>()["message"] = message;
            response->addHeader("Cache-Control", "no-store");
            response->setLength();
            request->send(response);
        }

        static std::optional<uint8_t> extractUint8Param(const AsyncWebServerRequest* req, const char* key)
        {
            if (!req->hasParam(key)) return std::nullopt;
            const auto value = std::clamp(req->getParam(key)->value().toInt(), 0l, 255l);
            return static_cast<uint8_t>(value);
        }
    };


#pragma pack(push, 1)
    struct Credentials
    {
        static constexpr auto MAX_USERNAME_LENGTH = 32;
        static constexpr auto MAX_PASSWORD_LENGTH = 32;

        std::array<char, MAX_USERNAME_LENGTH + 1> username = {};
        std::array<char, MAX_PASSWORD_LENGTH + 1> password = {};
    };
#pragma pack(pop)

    class Manager final : public BLE::Service
    {
        static constexpr auto LOG_TAG = "WebServerHandler";

        static constexpr auto PREFERENCES_NAME = "http";
        static constexpr auto PREFERENCES_USERNAME_KEY = "u";
        static constexpr auto PREFERENCES_PASSWORD_KEY = "p";

        AsyncWebServer webServer = AsyncWebServer(80);

        AsyncAuthenticationMiddleware authMiddleware;

    public:
        void begin(AsyncWebHandler* alexaHandler,
                   const std::vector<AsyncWebHandlerCreator*>&& httpHandlers)
        {
            if (alexaHandler != nullptr)
                webServer.addHandler(alexaHandler);
            // Alexa can't have authenticationMiddleware

            for (const auto& httpHandler : httpHandlers)
            {
                webServer.addHandler(httpHandler->createAsyncWebHandler())
                         .addMiddleware(&authMiddleware);
            }

            webServer.serveStatic("/", LittleFS, "/")
                     .setDefaultFile("index.html")
                     .setTryGzipFirst(true)
                     .setCacheControl("no-cache")
                     .addMiddleware(&authMiddleware);

            updateServerCredentials(getCredentials());
            webServer.begin();
        }

        [[nodiscard]] const AsyncAuthenticationMiddleware& getAuthenticationMiddleware() const
        {
            return authMiddleware;
        }

        void updateCredentials(const Credentials& credentials)
        {
            Preferences prefs;
            prefs.begin(PREFERENCES_NAME, false);
            prefs.putString(PREFERENCES_USERNAME_KEY, credentials.username.data());
            prefs.putString(PREFERENCES_PASSWORD_KEY, credentials.password.data());
            prefs.end();
            updateServerCredentials(credentials);
        }

        [[nodiscard]] static Credentials getCredentials()
        {
            Credentials credentials;
            if (Preferences prefs; prefs.begin(PREFERENCES_NAME, true))
            {
                strncpy(credentials.username.data(), prefs.getString(PREFERENCES_USERNAME_KEY, "admin").c_str(),
                        Credentials::MAX_USERNAME_LENGTH);
                strncpy(credentials.password.data(), prefs.getString(PREFERENCES_PASSWORD_KEY, "").c_str(),
                        Credentials::MAX_PASSWORD_LENGTH);
                prefs.end();
                return credentials;
            }
            Preferences prefs;
            prefs.begin(PREFERENCES_NAME, false);
            strncpy(credentials.username.data(), "admin", Credentials::MAX_USERNAME_LENGTH);
            strncpy(credentials.password.data(), generateRandomPassword().c_str(),
                    Credentials::MAX_PASSWORD_LENGTH);
            prefs.putString(PREFERENCES_USERNAME_KEY, credentials.username.data());
            prefs.putString(PREFERENCES_PASSWORD_KEY, credentials.password.data());
            prefs.end();
            return credentials;
        }

        void createServiceAndCharacteristics(NimBLEServer* server) override
        {
            const auto httpDetailsService = server->createService(BLE::UUID::HTTP_DETAILS_SERVICE);
            httpDetailsService->createCharacteristic(
                BLE::UUID::HTTP_CREDENTIALS_CHARACTERISTIC,
                READ | WRITE
            )->setCallbacks(new CredentialsCallback(this));
            httpDetailsService->start();
        }

        void clearServiceAndCharacteristics() override
        {
            ESP_LOGI(LOG_TAG, "No BLE pointers to be cleared");
        }

    private:
        void updateServerCredentials(const Credentials& credentials)
        {
            authMiddleware.setUsername(credentials.username.data());
            authMiddleware.setPassword(credentials.password.data());
            authMiddleware.setRealm("rgbw-ctrl");
            authMiddleware.setAuthFailureMessage("Authentication failed");
            authMiddleware.setAuthType(AUTH_BASIC);
            authMiddleware.generateHash();
        }

        [[nodiscard]] static String generateRandomPassword()
        {
            const auto v1 = random(100000, 999999);
            const auto v2 = random(100000, 999999);
            String password = String(v1) + "A-b" + String(v2);
            if (password.length() > Credentials::MAX_PASSWORD_LENGTH)
            {
                password = password.substring(0, Credentials::MAX_PASSWORD_LENGTH);
            }
            return password;
        }

        class CredentialsCallback final : public NimBLECharacteristicCallbacks
        {
            Manager* webServerHandler;

        public:
            explicit CredentialsCallback(Manager* webServerHandler) : webServerHandler(webServerHandler)
            {
            }

            void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
            {
                Credentials credentials;
                if (pCharacteristic->getValue().size() != sizeof(Credentials))
                {
                    ESP_LOGE(LOG_TAG, "Received invalid OTA credentials length: %d",
                             pCharacteristic->getValue().size());
                    return;
                }
                memcpy(&credentials, pCharacteristic->getValue().data(), sizeof(Credentials));
                webServerHandler->updateCredentials(credentials);
            }

            void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
            {
                Credentials credentials = getCredentials();
                pCharacteristic->setValue(reinterpret_cast<uint8_t*>(&credentials), sizeof(credentials));
            }
        };
    };
}
