#pragma once

#include <ArduinoJson.h>


class StateJsonFiller
{
public:
    virtual ~StateJsonFiller() = default;
    virtual void fillState(const JsonObject& root) const =0;
};
