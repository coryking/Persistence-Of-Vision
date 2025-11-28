#include "effects/SolidArms.h"
#include "polar_helpers.h"

/**
 * Render diagnostic test pattern - 20 discrete tests
 *
 * Each arm independently determines its pattern from its own angle.
 * This means all three arms can be showing different patterns at the same time.
 */
void SolidArms::render(RenderContext& ctx) {
    for (int a = 0; a < 3; a++) {
        auto& arm = ctx.arms[a];

        // Normalize angle and determine pattern (0-19)
        float normAngle = normalizeAngle(arm.angle);
        uint8_t pattern = static_cast<uint8_t>(normAngle / 18.0f);
        if (pattern > 19) pattern = 19;

        // Get color for this arm in this pattern
        CRGB armColor = getArmColor(pattern, a);
        bool striped = isStripedPattern(pattern);

        // Fill arm pixels
        for (int p = 0; p < 10; p++) {
            if (striped) {
                // Striped pattern - only positions 0, 4, 9
                if (p == 0 || p == 4 || p == 9) {
                    arm.pixels[p] = armColor;
                } else {
                    arm.pixels[p] = CRGB::Black;
                }
            } else {
                arm.pixels[p] = armColor;
            }
        }
    }
}

bool SolidArms::isStripedPattern(uint8_t pattern) const {
    return pattern >= 4 && pattern <= 7;
}

CRGB SolidArms::getArmColor(uint8_t pattern, uint8_t armIndex) const {
    // RGB rotation for multi-arm patterns
    static const CRGB rgbRotation[4][3] = {
        {CRGB(255, 0, 0), CRGB(0, 255, 0), CRGB(0, 0, 255)},  // R-G-B
        {CRGB(0, 255, 0), CRGB(0, 0, 255), CRGB(255, 0, 0)},  // G-B-R
        {CRGB(0, 0, 255), CRGB(255, 0, 0), CRGB(0, 255, 0)},  // B-R-G
        {CRGB::White, CRGB::White, CRGB::White}                // All white
    };

    // Single color sequence for individual arm tests
    static const CRGB singleColors[4] = {
        CRGB(255, 0, 0),  // Red
        CRGB(0, 255, 0),  // Green
        CRGB(0, 0, 255),  // Blue
        CRGB::White       // White
    };

    if (pattern <= 3) {
        // Patterns 0-3: Full RGB combinations (all arms lit)
        return rgbRotation[pattern][armIndex];
    }
    else if (pattern <= 7) {
        // Patterns 4-7: Striped RGB combinations
        return rgbRotation[pattern - 4][armIndex];
    }
    else if (pattern <= 11) {
        // Patterns 8-11: Arm A (index 0) only
        if (armIndex == 0) {
            return singleColors[pattern - 8];
        }
        return CRGB::Black;
    }
    else if (pattern <= 15) {
        // Patterns 12-15: Arm B (index 1) only
        if (armIndex == 1) {
            return singleColors[pattern - 12];
        }
        return CRGB::Black;
    }
    else {
        // Patterns 16-19: Arm C (index 2) only
        if (armIndex == 2) {
            return singleColors[pattern - 16];
        }
        return CRGB::Black;
    }
}
