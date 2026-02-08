#pragma once

#include <FastLED.h>

/**
 * @brief 16-bit RGB color type for high-precision color processing
 *
 * CRGB16 provides 16-bit precision per channel (0-65535) compared to CRGB's 8-bit (0-255).
 * This reduces banding in gradients and preserves precision through brightness scaling
 * before final downsampling to the hardware's 8-bit RGB + 5-bit brightness format.
 *
 * Part of fl_extensions - FastLED extensions designed as future PR candidates.
 */
struct CRGB16 {
    union {
        struct { uint16_t r, g, b; };
        struct { uint16_t red, green, blue; };
        uint16_t raw[3];
    };

    // Constructors
    inline CRGB16() : r(0), g(0), b(0) {}
    inline CRGB16(uint16_t r, uint16_t g, uint16_t b) : r(r), g(g), b(b) {}

    // Implicit promotion from 8-bit CRGB (×257 method for proper scaling)
    inline CRGB16(const CRGB& c)
        : r((uint16_t)c.r * 257), g((uint16_t)c.g * 257), b((uint16_t)c.b * 257) {}

    // Support CRGB::HTMLColorCode (Black, White, etc.)
    inline CRGB16(CRGB::HTMLColorCode colorcode)
        : CRGB16(CRGB(colorcode)) {}

    // Conversion from CHSV (hsv2rgb_rainbow then promote)
    CRGB16(const CHSV& hsv);

    // Convert back to 8-bit CRGB (truncate >>8)
    inline CRGB toCRGB() const {
        return CRGB(r >> 8, g >> 8, b >> 8);
    }

    // Saturating add (clamps at 65535)
    CRGB16& operator+=(const CRGB16& rhs);

    // Scale by 8-bit value (0-255) using scale16by8 per channel
    CRGB16& nscale8(uint8_t scale);

    // Constants
    static const CRGB16 Black;
    static const CRGB16 White;
};

// Free functions

/**
 * @brief Blend two CRGB16 colors
 * @param a First color
 * @param b Second color
 * @param amountOfB Blend amount (0 = all a, 255 = all b)
 * @return Blended color
 */
CRGB16 blend16(const CRGB16& a, const CRGB16& b, uint8_t amountOfB);

/**
 * @brief Fill array with solid color
 * @param arr Array to fill
 * @param count Number of elements
 * @param color Fill color
 */
void fill_solid(CRGB16* arr, int count, const CRGB16& color);
