#pragma once

#pragma pack(push, 1)
namespace EspNow
{
    struct Message
    {
        enum class Type : uint8_t
        {
            ToggleRed,
            ToggleGreen,
            ToggleBlue,
            ToggleWhite,
            ToggleAll,
            TurnOffAll,
            TurnOnAll,
            IncreaseBrightness,
            DecreaseBrightness,
        };

        Type type;
    };
}
#pragma pack(pop)
