#pragma once

#include <Update.h>
#include <optional>
#include <array>
#include <atomic>

namespace OTA
{
    enum class Status : uint8_t
    {
        Idle,
        Started,
        Completed,
        Failed
    };

#pragma pack(push, 1)
    struct State
    {
        Status status = Status::Idle;
        uint32_t totalBytesExpected = 0;
        uint32_t totalBytesReceived = 0;

        void toJson(const JsonObject& to) const
        {
            to["status"] = statusToString(status);
            to["totalBytesExpected"] = totalBytesExpected;
            to["totalBytesReceived"] = totalBytesReceived;
        }

        [[nodiscard]] static const char* statusToString(const Status status)
        {
            switch (status)
            {
            case Status::Idle: return "Idle";
            case Status::Started: return "Update in progress";
            case Status::Completed: return "Update completed successfully";
            case Status::Failed: return "Update failed";
            }
            return "Unknown state";
        }

        bool operator==(const State& other) const
        {
            return this->status == other.status &&
                this->totalBytesExpected == other.totalBytesExpected &&
                this->totalBytesReceived == other.totalBytesReceived;
        }

        bool operator!=(const State& other) const
        {
            return this->status != other.status ||
                this->totalBytesExpected != other.totalBytesExpected ||
                this->totalBytesReceived != other.totalBytesReceived;
        }
    };
#pragma pack(pop)

    class Handler final : public StateJsonFiller, public HTTP::AsyncWebHandlerCreator
    {
        static constexpr uint8_t MAX_UPDATE_ERROR_MSG_LEN = 64;

        const AsyncAuthenticationMiddleware& asyncAuthenticationMiddleware;

        // `status` is atomic because we can have concurrent http requests
        std::atomic<Status> status = Status::Idle;
        // `totalBytesExpected/Received` are volatile for visibility during upload monitoring only.
        volatile uint32_t totalBytesExpected = 0;
        volatile uint32_t totalBytesReceived = 0;

    public:
        explicit Handler(const AsyncAuthenticationMiddleware& asyncAuthenticationMiddleware)
            : asyncAuthenticationMiddleware(asyncAuthenticationMiddleware)
        {
        }

        [[nodiscard]] State getState() const
        {
            return {
                status.load(std::memory_order_relaxed),
                totalBytesExpected,
                totalBytesReceived
            };
        }

        [[nodiscard]] Status getStatus() const
        {
            return status.load(std::memory_order_relaxed);
        }

        void fillState(const JsonObject& root) const override
        {
            getState().toJson(root["ota"].to<JsonObject>());
        }

        AsyncWebHandler* createAsyncWebHandler() override
        {
            return new AsyncOtaWebHandler(*this);
        }

    private:
        class AsyncOtaWebHandler final : public AsyncWebHandler
        {
            static constexpr auto REALM = "rgbw-ctrl";
            static constexpr auto LOG_TAG = "OtaHandler";
            static constexpr auto ATTR_DOUBLE_REQUEST = "double-request";
            static constexpr auto ATTR_AUTHENTICATED = "authenticated";
            static constexpr auto AUTHORIZATION_HEADER = "Authorization";
            static constexpr auto CONTENT_LENGTH_HEADER = "Content-Length";
            static constexpr auto MSG_NO_AUTH = "Authentication required for OTA update";
            static constexpr auto MSG_WRONG_CREDENTIALS = "Wrong credentials";
            static constexpr auto MSG_ALREADY_IN_PROGRESS = "OTA update already in progress";
            static constexpr auto MSG_NO_SPACE = "Not enough space for OTA update";
            static constexpr auto MSG_UPLOAD_INCOMPLETE = "OTA upload not completed";
            static constexpr auto MSG_ALREADY_FINALIZED = "OTA update already finalized";
            static constexpr auto MSG_SUCCESS = "OTA update successful";

            Handler& handler;
            mutable std::optional<std::array<char, MAX_UPDATE_ERROR_MSG_LEN>> updateError;
            mutable bool uploadCompleted = false;

        public:
            explicit AsyncOtaWebHandler(Handler& handler): handler(handler)
            {
            }

        private:
            bool canHandle(AsyncWebServerRequest* request) const override
            {
                if (request->url() != HTTP::Endpoints::UPDATE)
                    return false;

                if (request->method() != HTTP_POST && request->method() != HTTP_GET)
                    return false;

                if (handler.asyncAuthenticationMiddleware.allowed(request))
                    request->setAttribute(ATTR_AUTHENTICATED, true);
                else
                    return true;

                if (handler.status == Status::Started)
                {
                    request->setAttribute(ATTR_DOUBLE_REQUEST, true);
                    return true;
                }

                resetUpdateState();
                handler.status = Status::Started;

                if (request->hasHeader(CONTENT_LENGTH_HEADER))
                    handler.totalBytesExpected = request->header(CONTENT_LENGTH_HEADER).toInt();

                if (request->hasParam("md5", false))
                {
                    const String& md5Param = request->getParam("md5")->value();
                    if (!Update.setMD5(md5Param.c_str()))
                    {
                        setUpdateError("Invalid MD5 format");
                        handler.status = Status::Failed;
                        return true;
                    }
                }

                request->onDisconnect([this]
                {
                    if (handler.status != Status::Completed)
                        Update.abort();
                    else
                        restartAfterUpdate();
                    resetUpdateState();
                });

                int updateTarget = U_FLASH;
                if (request->hasParam("name", false))
                {
                    const String& nameParam = request->getParam("name")->value();
                    updateTarget = nameParam == "filesystem" ? U_SPIFFS : U_FLASH;
                }

                if (const unsigned int expected = handler.totalBytesExpected;
                    Update.begin(expected == 0 ? UPDATE_SIZE_UNKNOWN : expected, updateTarget))
                {
                    ESP_LOGI(LOG_TAG, "Update started");
                }
                else
                {
                    handler.status = Status::Failed;
                    checkUpdateError();
                    ESP_LOGE(LOG_TAG, "Update.begin failed");
                }
                return true;
            }

            void handleRequest(AsyncWebServerRequest* request) override
            {
                if (request->method() == HTTP_GET)
                {
                    request->redirect("/ota.html");
                    return;
                }
                if (!request->hasAttribute(ATTR_AUTHENTICATED))
                {
                    const auto hasAuthorizationHeader = request->hasHeader(AUTHORIZATION_HEADER);
                    const auto authFailMsg = hasAuthorizationHeader ? MSG_WRONG_CREDENTIALS : MSG_NO_AUTH;
                    request->requestAuthentication(AUTH_BASIC, REALM, authFailMsg);
                    return;
                }
                if (request->hasAttribute(ATTR_DOUBLE_REQUEST))
                    return request->send(400, "text/plain", MSG_ALREADY_IN_PROGRESS);

                if (updateError)
                    return sendErrorResponse(request);

                if (handler.status != Status::Started)
                    return request->send(500, "text/plain", MSG_NO_SPACE);

                if (!uploadCompleted)
                {
                    ESP_LOGW(LOG_TAG, "OTA upload incomplete: received %lu of %lu bytes",
                             handler.totalBytesReceived, handler.totalBytesExpected);
                    handler.status = Status::Idle;
                    request->send(500, "text/plain", MSG_UPLOAD_INCOMPLETE);
                    return;
                }
                if (handler.status == Status::Completed)
                    return request->send(200, "text/plain", MSG_ALREADY_FINALIZED);

                if (Update.end(true))
                {
                    handler.status = Status::Completed;
                    ESP_LOGI(LOG_TAG, "Update successfully completed");
                    request->send(200, "text/plain", MSG_SUCCESS);
                }
                else
                {
                    handler.status = Status::Failed;
                    checkUpdateError();
                    sendErrorResponse(request);
                }
            }

            void handleUpload(
                AsyncWebServerRequest* request,
                const String& filename,
                const size_t index,
                uint8_t* data,
                const size_t len,
                const bool final
            ) override
            {
                if (handler.status != Status::Started) return;
                if (!isRequestValidForUpload(request)) return;

                if (Update.write(data, len) != len)
                {
                    handler.status = Status::Failed;
                    checkUpdateError();
                    return;
                }

                handler.totalBytesReceived += len;

                if (final) uploadCompleted = true;
            }

            void handleBody(
                AsyncWebServerRequest* request,
                uint8_t* data,
                const size_t len,
                const size_t index,
                const size_t total
            ) override
            {
                if (handler.status != Status::Started) return;
                if (!isRequestValidForUpload(request)) return;

                if (Update.write(data, len) != len)
                {
                    handler.status = Status::Failed;
                    checkUpdateError();
                    return;
                }

                handler.totalBytesReceived += len;

                if (index + len >= total)
                    uploadCompleted = true;
            }

            void sendErrorResponse(AsyncWebServerRequest* request) const
            {
                request->send(500, "text/plain", updateError.has_value() ? updateError->data() : "Unknown OTA error");
                updateError.reset();
            }

            void checkUpdateError() const
            {
                const char* error = Update.errorString();
                setUpdateError(error);
            }

            void setUpdateError(const char* error) const
            {
                std::array<char, MAX_UPDATE_ERROR_MSG_LEN> buffer = {};
                strncpy(buffer.data(), error, MAX_UPDATE_ERROR_MSG_LEN - 1);
                updateError = buffer;
                ESP_LOGE(LOG_TAG, "Update error: %s", error);
            }

            void resetUpdateState() const
            {
                handler.status = Status::Idle;
                handler.totalBytesExpected = 0;
                handler.totalBytesReceived = 0;
                uploadCompleted = false;
                updateError.reset();
            }

            static void restartAfterUpdate()
            {
                ESP_LOGI(LOG_TAG, "Restarting device after OTA update...");
                async_call([]
                {
                    esp_restart();
                }, 2048, 100);
            }

            static bool isRequestValidForUpload(const AsyncWebServerRequest* request)
            {
                return request->hasAttribute(ATTR_AUTHENTICATED)
                    && !request->hasAttribute(ATTR_DOUBLE_REQUEST);
            }
        };
    };
}
