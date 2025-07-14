#pragma once

#include <array>
#include <algorithm>
#include <cmath>

class AsyncEspAlexaColorUtils
{
public:
    // Alexa-specific limits
    static constexpr uint8_t ALEXA_MIN_BRI_VAL = 1;
    static constexpr uint8_t ALEXA_MAX_BRI_VAL = 254;
    static constexpr uint8_t ALEXA_MIN_SAT_VAL = 0;
    static constexpr uint8_t ALEXA_MAX_SAT_VAL = 254;

    // RGB range
    static constexpr uint8_t RGB_MIN_VAL = 0;
    static constexpr uint8_t RGB_MAX_VAL = 255;

    // Hue range (Alexa expects 0-65535)
    static constexpr uint16_t HUE_MIN_VAL = 0;
    static constexpr uint16_t HUE_MAX_VAL = 65535;

    // Color temperature in Mireds (~6500K to ~2000K)
    static constexpr uint16_t CT_MIN_MIREDS = 153;
    static constexpr uint16_t CT_MAX_MIREDS = 500;
    static constexpr float KELVIN_MIN = 2000.0f;
    static constexpr float KELVIN_MAX = 6500.0f;

    // RGB to XY conversion constants
    static constexpr float RGB_X_COEFF[3] = {0.4124f, 0.3576f, 0.1805f};
    static constexpr float RGB_Y_COEFF[3] = {0.2126f, 0.7152f, 0.0722f};
    static constexpr float RGB_Z_COEFF[3] = {0.0193f, 0.1192f, 0.9505f};
    static constexpr float LINEAR_THRESHOLD = 0.04045f;
    static constexpr float LINEAR_SCALE = 12.92f;
    static constexpr float LINEAR_OFFSET = 0.055f;
    static constexpr float LINEAR_DIVISOR = 1.055f;
    static constexpr float LINEAR_EXPONENT = 2.4f;

    // CT conversion constants (Tanner Helland)
    static constexpr float CT_GREEN_A = 99.470802f;
    static constexpr float CT_GREEN_B = -161.119568f;
    static constexpr float CT_BLUE_A = 138.517731f;
    static constexpr float CT_BLUE_B = -305.044793f;
    static constexpr float CT_RED_A = 329.698727f;
    static constexpr float CT_RED_EXP = -0.13320476f;
    static constexpr float CT_GREEN2_A = 288.12217f;
    static constexpr float CT_GREEN2_EXP = -0.07551485f;

    static constexpr float HUE_SCALE = 65535.0f / 360.0f;
    static constexpr float KELVIN_TO_MIREDS = 1000000.0f;

    static constexpr uint16_t  DEFAULT_CT_WHEN_OFF = CT_MAX_MIREDS;
    static constexpr uint8_t DEFAULT_BRI_WHEN_OFF = ALEXA_MIN_BRI_VAL;

    // Returns true if the RGB color is near grayscale (i.e., CT-representable)
    [[nodiscard]] static bool isCtLikeColor(
        const uint8_t r, const uint8_t g, const uint8_t b, const float maxChromaDiff = 20.0f)
    {
        const auto rf = static_cast<float>(r);
        const auto gf = static_cast<float>(g);
        const auto bf = static_cast<float>(b);
        const float chroma = std::max({std::abs(rf - gf), std::abs(rf - bf), std::abs(gf - bf)});
        return chroma <= maxChromaDiff;
    }

    [[nodiscard]] static std::tuple<uint16_t, uint8_t, uint8_t> xyToHsv(
        const float x, const float y, const uint8_t brightness = RGB_MAX_VAL)
    {
        // Simplified XY to RGB linear to HSV conversion (limited use, low precision)
        if (x <= 0.0f || y <= 0.0f || y >= 1.0f) return {0, 0, 0};
        const float z = 1.0f - x - y;
        const float Y = static_cast<float>(brightness) / RGB_MAX_VAL;
        const float X = Y / y * x;
        const float Z = Y / y * z;

        float r = X * 3.2406f + Y * -1.5372f + Z * -0.4986f;
        float g = X * -0.9689f + Y * 1.8758f + Z * 0.0415f;
        float b = X * 0.0557f + Y * -0.2040f + Z * 1.0570f;

        r = r <= 0.0031308f ? 12.92f * r : 1.055f * pow(r, 1.0f / 2.4f) - 0.055f;
        g = g <= 0.0031308f ? 12.92f * g : 1.055f * pow(g, 1.0f / 2.4f) - 0.055f;
        b = b <= 0.0031308f ? 12.92f * b : 1.055f * pow(b, 1.0f / 2.4f) - 0.055f;

        return rgbToHsv(
            static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * RGB_MAX_VAL),
            static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * RGB_MAX_VAL),
            static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * RGB_MAX_VAL)
        );
    }

    [[nodiscard]] static std::array<uint8_t, 3> hsToRgb(
        const uint16_t hue, const uint8_t saturation)
    {
        std::array<uint8_t, 3> rgb = {};
        const float h = static_cast<float>(hue) / (HUE_MAX_VAL - HUE_MIN_VAL);
        const float s = static_cast<float>(saturation) / (ALEXA_MAX_SAT_VAL - ALEXA_MIN_SAT_VAL);
        const int i = static_cast<int>(h * 6.0f);
        const float f = h * 6.0f - static_cast<float>(i);
        const float p = RGB_MAX_VAL * (1 - s);
        const float q = RGB_MAX_VAL * (1 - f * s);
        const float t = RGB_MAX_VAL * (1 - (1 - f) * s);
        switch (i % 6)
        {
        case 0: rgb[0] = RGB_MAX_VAL, rgb[1] = static_cast<uint8_t>(t), rgb[2] = static_cast<uint8_t>(p);
            break;
        case 1: rgb[0] = static_cast<uint8_t>(q), rgb[1] = RGB_MAX_VAL, rgb[2] = static_cast<uint8_t>(p);
            break;
        case 2: rgb[0] = static_cast<uint8_t>(p), rgb[1] = RGB_MAX_VAL, rgb[2] = static_cast<uint8_t>(t);
            break;
        case 3: rgb[0] = static_cast<uint8_t>(p), rgb[1] = static_cast<uint8_t>(q), rgb[2] = RGB_MAX_VAL;
            break;
        case 4: rgb[0] = static_cast<uint8_t>(t), rgb[1] = static_cast<uint8_t>(p), rgb[2] = RGB_MAX_VAL;
            break;
        case 5: rgb[0] = RGB_MAX_VAL, rgb[1] = static_cast<uint8_t>(p), rgb[2] = static_cast<uint8_t>(q);
            break;
        default: rgb[0] = rgb[1] = rgb[2] = RGB_MIN_VAL;
            break;
        }
        return rgb;
    }

    [[nodiscard]] static std::array<uint8_t, 3> hsvToRgb(
        const uint16_t hue, const uint8_t saturation, const uint8_t value)
    {
        const auto [r, g, b] = hsToRgb(hue, saturation);
        const float factor = static_cast<float>(value) / (ALEXA_MAX_BRI_VAL - ALEXA_MIN_BRI_VAL);
        return {
            static_cast<uint8_t>(std::clamp(static_cast<float>(r) * factor,
                                            static_cast<float>(RGB_MIN_VAL),
                                            static_cast<float>(RGB_MAX_VAL))
            ),
            static_cast<uint8_t>(std::clamp(static_cast<float>(g) * factor,
                                            static_cast<float>(RGB_MIN_VAL),
                                            static_cast<float>(RGB_MAX_VAL))
            ),
            static_cast<uint8_t>(std::clamp(static_cast<float>(b) * factor,
                                            static_cast<float>(RGB_MIN_VAL),
                                            static_cast<float>(RGB_MAX_VAL))
            )
        };
    }

    [[nodiscard]] static std::array<uint8_t, 4> hsvToRgbw(const uint16_t hue, const uint8_t saturation,
                                                          const uint8_t value)
    {
        const auto [r, g, b] = hsvToRgb(hue, saturation, value);
        const uint8_t w = std::min({r, g, b});
        return {
            static_cast<uint8_t>(r - w),
            static_cast<uint8_t>(g - w),
            static_cast<uint8_t>(b - w),
            w
        };
    }

    [[nodiscard]] static std::array<uint8_t, 3> ctToRgb(const uint8_t brightness, const uint16_t colorTemperature)
    {
        // Convert Mireds to Kelvin
        const float kelvin = 1000000.0f / static_cast<float>(colorTemperature);
        const auto [r, g, b] = kelvinToRgb(kelvin);
        const float factor = static_cast<float>(brightness) / RGB_MAX_VAL;
        return {
            static_cast<uint8_t>(r * factor),
            static_cast<uint8_t>(g * factor),
            static_cast<uint8_t>(b * factor),
        };
    }

    [[nodiscard]] static std::array<uint8_t, 4> ctToRgbw(const uint8_t brightness, const uint16_t colorTemperature)
    {
        const auto [r, g, b] = ctToRgb(brightness, colorTemperature);
        const uint8_t w = std::min({r, g, b});
        return {
            static_cast<uint8_t>(r - w),
            static_cast<uint8_t>(g - w),
            static_cast<uint8_t>(b - w),
            w
        };
    }

    [[nodiscard]] static std::array<float, 2> hsvToXY(
        const uint16_t hue, const uint8_t saturation, const uint8_t value = RGB_MAX_VAL)
    {
        const auto [r8, g8, b8] = hsvToRgb(hue, saturation, value);
        float r = static_cast<float>(r8) / RGB_MAX_VAL;
        float g = static_cast<float>(g8) / RGB_MAX_VAL;
        float b = static_cast<float>(b8) / RGB_MAX_VAL;

        // linearize
        r = r > LINEAR_THRESHOLD ? pow((r + LINEAR_OFFSET) / LINEAR_DIVISOR, LINEAR_EXPONENT) : r / LINEAR_SCALE;
        g = g > LINEAR_THRESHOLD ? pow((g + LINEAR_OFFSET) / LINEAR_DIVISOR, LINEAR_EXPONENT) : g / LINEAR_SCALE;
        b = b > LINEAR_THRESHOLD ? pow((b + LINEAR_OFFSET) / LINEAR_DIVISOR, LINEAR_EXPONENT) : b / LINEAR_SCALE;

        // RGB → XYZ
        const float X = r * RGB_X_COEFF[0] + g * RGB_X_COEFF[1] + b * RGB_X_COEFF[2];
        const float Y = r * RGB_Y_COEFF[0] + g * RGB_Y_COEFF[1] + b * RGB_Y_COEFF[2];
        const float Z = r * RGB_Z_COEFF[0] + g * RGB_Z_COEFF[1] + b * RGB_Z_COEFF[2];

        const float sum = X + Y + Z;
        if (sum == 0.0f) return {0.0f, 0.0f};

        return {X / sum, Y / sum};
    }

    [[nodiscard]] static std::tuple<uint16_t, uint8_t, uint8_t> rgbToHsv(
        const uint8_t r, const uint8_t g, const uint8_t b)
    {
        float fr = static_cast<float>(r) / static_cast<float>(RGB_MAX_VAL - RGB_MIN_VAL);
        float fg = static_cast<float>(g) / static_cast<float>(RGB_MAX_VAL - RGB_MIN_VAL);
        float fb = static_cast<float>(b) / static_cast<float>(RGB_MAX_VAL - RGB_MIN_VAL);

        const float max = std::max({fr, fg, fb});
        const float min = std::min({fr, fg, fb});
        const float delta = max - min;

        float h = 0.0f;
        if (delta > 0.0f)
        {
            if (max == fr)
                h = 60.0f * fmod((fg - fb) / delta, 6.0f);
            else if (max == fg)
                h = 60.0f * ((fb - fr) / delta + 2.0f);
            else
                h = 60.0f * ((fr - fg) / delta + 4.0f);
        }
        if (h < 0.0f) h += 360.0f;

        const float s = max == 0.0f ? 0.0f : delta / max;
        const float v = max;

        const auto hue = static_cast<uint16_t>(h / 360.0f * 65535.0f);
        const auto sat = static_cast<uint8_t>(s * ALEXA_MAX_SAT_VAL);
        const auto val = std::max(static_cast<uint8_t>(v * ALEXA_MAX_BRI_VAL), ALEXA_MIN_BRI_VAL);

        return {hue, sat, val};
    }

    [[nodiscard]] static std::tuple<uint16_t, uint8_t, uint8_t> rgbwToHsv(
        const uint8_t r, const uint8_t g, const uint8_t b, const uint8_t w)
    {
        const uint8_t r_adj = static_cast<uint8_t>(std::min<int>(RGB_MAX_VAL, r + w));
        const uint8_t g_adj = static_cast<uint8_t>(std::min<int>(RGB_MAX_VAL, g + w));
        const uint8_t b_adj = static_cast<uint8_t>(std::min<int>(RGB_MAX_VAL, b + w));

        return rgbToHsv(r_adj, g_adj, b_adj);
    }

    [[nodiscard]] static std::tuple<uint8_t, uint16_t> rgbToCtBrightness(
        const uint8_t r, const uint8_t g, const uint8_t b)
    {
        const auto rf = static_cast<float>(r);
        const auto gf = static_cast<float>(g);
        const auto bf = static_cast<float>(b);

        const float brightness = std::max({rf, gf, bf});
        if (brightness == 0.0f) return {0, DEFAULT_CT_WHEN_OFF};

        if (!isCtLikeColor(r, g, b))
            return {static_cast<uint8_t>(brightness), DEFAULT_CT_WHEN_OFF};

        const float r_norm = rf / brightness;
        const float g_norm = gf / brightness;
        const float b_norm = bf / brightness;

        float kelvin = 0.0f;
        if (r_norm >= b_norm)
        {
            // approximate cooler temps
            kelvin = 1000.0f * exp((b_norm - CT_BLUE_B) / CT_BLUE_A);
        }
        else
        {
            // approximate warmer temps
            kelvin = 100.0f * exp((g_norm - CT_GREEN_B) / CT_GREEN_A);
        }

        kelvin = std::clamp(kelvin, KELVIN_MIN, KELVIN_MAX);
        const auto ct = static_cast<uint16_t>(KELVIN_TO_MIREDS / kelvin);
        const auto bri = static_cast<uint8_t>(
            std::clamp(
                brightness,
                static_cast<float>(RGB_MIN_VAL),
                static_cast<float>(RGB_MAX_VAL)
            )
        );
        return {bri, ct};
    }

    [[nodiscard]] static std::tuple<float, float, float> kelvinToRgb(float kelvin)
    {
        kelvin = std::clamp(kelvin, KELVIN_MIN, KELVIN_MAX);
        float r, g, b;

        if (kelvin <= 6600.0f)
        {
            r = RGB_MAX_VAL;
            g = std::clamp(CT_GREEN_A * log(kelvin / 100.0f) + CT_GREEN_B, static_cast<float>(RGB_MIN_VAL),
                           static_cast<float>(RGB_MAX_VAL));
            b = kelvin <= 1900.0f
                    ? RGB_MIN_VAL
                    : std::clamp(
                        CT_BLUE_A * log((kelvin - 1000.0f) / 100.0f) + CT_BLUE_B,
                        static_cast<float>(RGB_MIN_VAL),
                        static_cast<float>(RGB_MAX_VAL)
                    );
        }
        else
        {
            r = std::clamp(
                CT_RED_A * pow((kelvin - 6000.0f) / 100.0f,
                               CT_RED_EXP),
                static_cast<float>(RGB_MIN_VAL),
                static_cast<float>(RGB_MAX_VAL)
            );
            g = std::clamp(
                CT_GREEN2_A * pow((kelvin - 6000.0f) / 100.0f,
                                  CT_GREEN2_EXP),
                static_cast<float>(RGB_MIN_VAL),
                static_cast<float>(RGB_MAX_VAL)
            );
            b = RGB_MAX_VAL;
        }
        return {r, g, b};
    }
};
