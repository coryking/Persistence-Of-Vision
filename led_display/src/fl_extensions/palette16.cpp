#include "fl_extensions/palette16.h"

// CRGBPalette16 version
CRGB16 ColorFromPalette16(const CRGBPalette16& pal, uint16_t index,
                          uint8_t brightness, TBlendType blendType) {
    // Index math: high 4 bits = entry (0-15), low 12 bits = blend fraction
    uint8_t entry = (index >> 12) & 0x0F;  // 0-15
    uint16_t fraction = (index & 0x0FFF) << 4;  // Scale 0-4095 → 0-65520

    CRGB16 result;

    if (blendType == NOBLEND || fraction == 0) {
        // No blending - just promote the palette entry
        CRGB color8 = pal.entries[entry];
        result = CRGB16(color8);
    } else {
        // Linear blend between two palette entries
        uint8_t nextEntry = (entry == 15) ? 0 : entry + 1;

        CRGB color1_8 = pal.entries[entry];
        CRGB color2_8 = pal.entries[nextEntry];

        // Promote to 16-bit
        CRGB16 color1(color1_8);
        CRGB16 color2(color2_8);

        // Interpolate with 16-bit precision
        result.r = lerp16by16(color1.r, color2.r, fraction);
        result.g = lerp16by16(color1.g, color2.g, fraction);
        result.b = lerp16by16(color1.b, color2.b, fraction);
    }

    // Apply brightness scaling
    if (brightness != 255) {
        result.nscale8(brightness);
    }

    return result;
}

// CRGBPalette256 version
CRGB16 ColorFromPalette16(const CRGBPalette256& pal, uint16_t index,
                          uint8_t brightness, TBlendType blendType) {
    // Index math: high 8 bits = entry (0-255), low 8 bits = blend fraction
    uint8_t entry = index >> 8;  // 0-255
    uint8_t fraction8 = index & 0xFF;  // 0-255
    uint16_t fraction = (uint16_t)fraction8 * 257;  // Scale to 0-65535

    CRGB16 result;

    if (blendType == NOBLEND || fraction == 0) {
        // No blending - just promote the palette entry
        CRGB color8 = pal.entries[entry];
        result = CRGB16(color8);
    } else {
        // Linear blend between two palette entries
        uint8_t nextEntry = (entry == 255) ? 0 : entry + 1;

        CRGB color1_8 = pal.entries[entry];
        CRGB color2_8 = pal.entries[nextEntry];

        // Promote to 16-bit
        CRGB16 color1(color1_8);
        CRGB16 color2(color2_8);

        // Interpolate with 16-bit precision
        result.r = lerp16by16(color1.r, color2.r, fraction);
        result.g = lerp16by16(color1.g, color2.g, fraction);
        result.b = lerp16by16(color1.b, color2.b, fraction);
    }

    // Apply brightness scaling
    if (brightness != 255) {
        result.nscale8(brightness);
    }

    return result;
}
