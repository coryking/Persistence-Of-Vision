#include "effects/SolidArms.h"
#include "polar_helpers.h"

// Mutual exclusion guard: pipeline profiler vs effect-specific timing
// TEMPORARILY DISABLED FOR TESTING
// #if defined(ENABLE_EFFECT_TIMING) && defined(ENABLE_TIMING_INSTRUMENTATION)
// #error "ENABLE_EFFECT_TIMING and ENABLE_TIMING_INSTRUMENTATION are mutually exclusive"
// #endif

#ifdef ENABLE_EFFECT_TIMING
#include "esp_log.h"
static const char* TAG = "SOLIDARMS";
static uint8_t prevPatterns[3] = {255, 255, 255};
static uint32_t boundaryHitCount[3] = {0, 0, 0};
static uint32_t patternChangeCount[3] = {0, 0, 0};
static constexpr angle_t BOUNDARY_EPSILON = 5;  // 0.5 degrees from boundary (5 units)

// Check if angle is near a pattern boundary (multiple of 18° = 180 units)
static bool isNearBoundary(angle_t angleUnits, angle_t* distToBoundary) {
    angle_t remainder = angleUnits % ANGLE_PER_PATTERN;
    angle_t dist = (remainder < (ANGLE_PER_PATTERN / 2)) ? remainder : (ANGLE_PER_PATTERN - remainder);
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

    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        // Pattern = angleUnits / 180 (exact integer division!)
        // 20 patterns * 18° each = 360° exactly
        // angleUnits is already normalized to 0-3599
        uint8_t pattern = arm.angle / ANGLE_PER_PATTERN;
        if (pattern > 19) pattern = 19;

#ifdef ENABLE_EFFECT_TIMING
        // Detect boundary proximity
        angle_t distToBoundary;
        bool nearBoundary = isNearBoundary(arm.angle, &distToBoundary);

        // Detect pattern changes (flickering)
        bool patternChanged = (prevPatterns[a] != 255 && prevPatterns[a] != pattern);

        if (nearBoundary) {
            boundaryHitCount[a]++;
        }

        if (patternChanged) {
            patternChangeCount[a]++;
            // Log pattern transitions (not flickering - just normal progression)
            if ((prevPatterns[a] == 15 && pattern == 16) ||
                (prevPatterns[a] == 16 && pattern == 15)) {
                ESP_LOGD(TAG, "PATTERN_288: arm%d frame=%u angle=%u pat=%d->%d dist=%u",
                              a, ctx.frameNumber, arm.angle, prevPatterns[a], pattern, distToBoundary);
            }
            // Log any other pattern boundary transitions
            else if (nearBoundary) {
                ESP_LOGD(TAG, "PATTERN_TRANSITION: arm%d frame=%u angle=%u pat=%d->%d dist=%u",
                              a, ctx.frameNumber, arm.angle, prevPatterns[a], pattern, distToBoundary);
            }
        }

        prevPatterns[a] = pattern;

        // Periodic summary every 10000 frames
        if (a == 0 && ctx.frameNumber % 10000 == 0) {
            ESP_LOGD(TAG, "BOUNDARY_STATS@%u: hits=[%u,%u,%u] changes=[%u,%u,%u]",
                          ctx.frameNumber,
                          boundaryHitCount[0], boundaryHitCount[1], boundaryHitCount[2],
                          patternChangeCount[0], patternChangeCount[1], patternChangeCount[2]);
        }
#endif

        // Get color for this arm in this pattern (CRGB promotes to CRGB16)
        CRGB16 armColor = getArmColor(pattern, a);
        bool striped = isStripedPattern(pattern);

        // Fill arm pixels
        for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++) {
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

    // Reference marker: 30-pixel radial line at physical 0° (hall sensor position)
    // Each arm lights up when IT crosses 0°, creating one continuous radial line
    // At 2800 RPM, ~3° per frame, so check within 3° of 0°
    // 3 degrees = 30 units, 357 degrees = 3570 units
    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        angle_t armAngle = ctx.arms[a].angle;
        if (armAngle < 30 || armAngle > 3570) {
            CRGB16 color = (armAngle < 30) ? CRGB::White : CRGB::Orange;
            fill_solid(ctx.arms[a].pixels, HardwareConfig::LEDS_PER_ARM, color);
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
