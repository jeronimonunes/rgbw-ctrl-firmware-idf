#pragma once

#include <ArduinoJson.h>
#include <AsyncJson.h>

#include "wifi_manager.hh"

class StateRestHandler final : public HTTP::AsyncWebHandlerCreator
{
    std::vector<StateJsonFiller*> jsonStateFillers;

public:
    explicit StateRestHandler(const std::vector<StateJsonFiller*>&& jsonStateFillers)
        : jsonStateFillers(jsonStateFillers)
    {
    }

    AsyncWebHandler* createAsyncWebHandler() override
    {
        return new AsyncRestWebHandler(this);
    }

private:
    class AsyncRestWebHandler final : public AsyncWebHandler
    {
        StateRestHandler* restHandler;

    public:
        explicit AsyncRestWebHandler(StateRestHandler* restHandler): restHandler(restHandler)
        {
        }

    private:
        bool canHandle(AsyncWebServerRequest* request) const override
        {
            return request->method() == HTTP_GET && request->url() == HTTP::Endpoints::STATE;
        }

        void handleRequest(AsyncWebServerRequest* request) override
        {
            const auto response = new AsyncJsonResponse();
            const auto doc = response->getRoot().to<JsonObject>();
            for (const auto& filler : restHandler->jsonStateFillers)
            {
                filler->fillState(doc);
            }
            response->addHeader("Cache-Control", "no-store");
            response->setLength();
            request->send(response);
        }
    };
};
