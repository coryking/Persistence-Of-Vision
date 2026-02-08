#include "fl_extensions/crgb16.h"

// Constants
const CRGB16 CRGB16::Black = CRGB16(0, 0, 0);
const CRGB16 CRGB16::White = CRGB16(65535, 65535, 65535);

// CHSV constructor
CRGB16::CRGB16(const CHSV& hsv) {
    // Convert CHSV to 8-bit CRGB first, then promote
    CRGB temp;
    hsv2rgb_rainbow(hsv, temp);
    r = (uint16_t)temp.r * 257;
    g = (uint16_t)temp.g * 257;
    b = (uint16_t)temp.b * 257;
}

// Saturating add
CRGB16& CRGB16::operator+=(const CRGB16& rhs) {
    uint32_t sum;

    sum = (uint32_t)r + rhs.r;
    r = (sum > 65535) ? 65535 : sum;

    sum = (uint32_t)g + rhs.g;
    g = (sum > 65535) ? 65535 : sum;

    sum = (uint32_t)b + rhs.b;
    b = (sum > 65535) ? 65535 : sum;

    return *this;
}

// Scale by 8-bit value
CRGB16& CRGB16::nscale8(uint8_t scale) {
    r = scale16by8(r, scale);
    g = scale16by8(g, scale);
    b = scale16by8(b, scale);
    return *this;
}

// Free functions

CRGB16 blend16(const CRGB16& a, const CRGB16& b, uint8_t amountOfB) {
    uint8_t amountOfA = 255 - amountOfB;

    CRGB16 result;
    result.r = lerp16by8(a.r, b.r, amountOfB);
    result.g = lerp16by8(a.g, b.g, amountOfB);
    result.b = lerp16by8(a.b, b.b, amountOfB);

    return result;
}

void fill_solid(CRGB16* arr, int count, const CRGB16& color) {
    for (int i = 0; i < count; i++) {
        arr[i] = color;
    }
}
