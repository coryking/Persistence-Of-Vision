#include "effects/SolidArms.h"
#include "polar_helpers.h"

#ifdef ENABLE_DETAILED_TIMING
#include <Arduino.h>
static uint8_t prevPatterns[3] = {255, 255, 255};
static uint32_t boundaryHitCount[3] = {0, 0, 0};
static uint32_t patternChangeCount[3] = {0, 0, 0};
static uint32_t frameCount = 0;
static constexpr float BOUNDARY_EPSILON = 0.5f;  // degrees from boundary to flag

// Check if angle is near a pattern boundary (multiple of 18°)
static bool isNearBoundary(float angle, float* distToBoundary) {
    float remainder = fmod(angle, 18.0f);
    float dist = (remainder < 9.0f) ? remainder : (18.0f - remainder);
    if (distToBoundary) *distToBoundary = dist;
    return dist < BOUNDARY_EPSILON;
}
#endif

/**
 * Render diagnostic test pattern - 20 discrete tests
 *
 * Each arm independently determines its pattern from its own angle.
 * This means all three arms can be showing different patterns at the same time.
 */
void SolidArms::render(RenderContext& ctx) {
#ifdef ENABLE_DETAILED_TIMING
    frameCount++;
#endif

    for (int a = 0; a < 3; a++) {
        auto& arm = ctx.arms[a];

        // Normalize angle and determine pattern (0-19)
        float normAngle = normalizeAngle(arm.angle);
        uint8_t pattern = static_cast<uint8_t>(normAngle / 18.0f);
        if (pattern > 19) pattern = 19;

#ifdef ENABLE_DETAILED_TIMING
        // Detect boundary proximity
        float distToBoundary;
        bool nearBoundary = isNearBoundary(normAngle, &distToBoundary);

        // Detect pattern changes (flickering)
        bool patternChanged = (prevPatterns[a] != 255 && prevPatterns[a] != pattern);

        if (nearBoundary) {
            boundaryHitCount[a]++;
        }

        if (patternChanged) {
            patternChangeCount[a]++;
            // Log pattern changes near the critical 288° boundary (pattern 15→16)
            if ((prevPatterns[a] == 15 && pattern == 16) ||
                (prevPatterns[a] == 16 && pattern == 15)) {
                Serial.printf("FLICKER@288: arm%d frame=%u angle=%.4f pat=%d->%d dist=%.4f\n",
                              a, frameCount, normAngle, prevPatterns[a], pattern, distToBoundary);
            }
            // Also log any boundary flickering
            else if (nearBoundary) {
                Serial.printf("BOUNDARY_FLICKER: arm%d frame=%u angle=%.4f pat=%d->%d dist=%.4f\n",
                              a, frameCount, normAngle, prevPatterns[a], pattern, distToBoundary);
            }
        }

        prevPatterns[a] = pattern;

        // Periodic summary every 10000 frames
        if (a == 0 && frameCount % 10000 == 0) {
            Serial.printf("BOUNDARY_STATS@%u: hits=[%u,%u,%u] changes=[%u,%u,%u]\n",
                          frameCount,
                          boundaryHitCount[0], boundaryHitCount[1], boundaryHitCount[2],
                          patternChangeCount[0], patternChangeCount[1], patternChangeCount[2]);
        }
#endif

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

        // Reference marker: white line at 0° for one render cycle
        // At 2800 RPM, ~3° per frame, so check within 3° of 0°
        if (normAngle < 3.0f || normAngle > 357.0f) {
            for (int p = 0; p < 10; p++) {
                if(normAngle < 3.0f)
                  arm.pixels[p] = CRGB::White;
                else
                  arm.pixels[p] = CRGB::Orange;
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
