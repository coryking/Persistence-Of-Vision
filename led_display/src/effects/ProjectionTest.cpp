#include "effects/ProjectionTest.h"
#include "hardware_config.h"
#include "geometry.h"
#include <FastLED.h>  // for sin8

// Jupiter-ish palette: alternating tan/brown/cream bands
static const CRGB bandColors[] = {
    CRGB(210, 180, 140),  // tan
    CRGB(139, 90, 43),    // brown
    CRGB(255, 248, 220),  // cream
    CRGB(160, 82, 45),    // sienna
    CRGB(222, 184, 135),  // burlywood
    CRGB(101, 67, 33),    // dark brown
};
static constexpr uint8_t NUM_BANDS = sizeof(bandColors) / sizeof(bandColors[0]);

// Physical radius constants (from geometry.h, scaled to integer math)
// Using 8-bit fixed point: actual_mm = value / 2
static constexpr uint8_t INNER_RADIUS_SCALED = 20;   // 10mm * 2
static constexpr uint8_t OUTER_RADIUS_SCALED = 202;  // 101mm * 2
static constexpr uint8_t RADIUS_SPAN_SCALED = OUTER_RADIUS_SCALED - INNER_RADIUS_SCALED;

void ProjectionTest::render(RenderContext& ctx) {
    // Rotate ~36°/second (full rotation in 10 sec)
    // Use frame time for consistency across RPM
    static uint32_t lastTimeUs = 0;
    uint32_t deltaUs = ctx.timeUs - lastTimeUs;
    lastTimeUs = ctx.timeUs;

    // 360° in 10 sec = 3600 units in 10,000,000 us
    rotationOffset += (deltaUs * 36) / 100000;
    rotationOffset %= ANGLE_FULL_CIRCLE;

    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        // Effective angle for projection (disc angle + rotation offset)
        uint16_t effectiveAngle = (arm.angleUnits + rotationOffset) % ANGLE_FULL_CIRCLE;

        // sin8 takes 0-255 (maps to 0-360°), returns 0-255 (where 128 = sin(0) = 0)
        uint8_t angle8 = (effectiveAngle * 256UL) / ANGLE_FULL_CIRCLE;
        int16_t sinCentered = (int16_t)sin8(angle8) - 128;  // -128 to +127

        for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++) {
            // Physical radius: INNER + (p / max_p) * SPAN
            // Scaled by 2 to keep integer math
            uint8_t radiusScaled = INNER_RADIUS_SCALED +
                (p * RADIUS_SPAN_SCALED) / (HardwareConfig::LEDS_PER_ARM - 1);

            // y = r * sin(θ)
            // radiusScaled is 20-202 (10-101mm * 2)
            // sinCentered is -128 to +127 (representing -1 to +1)
            // y ranges from about -202 to +202 (scaled)
            int16_t y = (radiusScaled * sinCentered) / 128;

            // Map y from [-OUTER, +OUTER] to [0, 255] for band indexing
            // y + OUTER_RADIUS_SCALED maps to [0, 2*OUTER]
            // Then scale to 0-255
            int16_t yNormalized = ((y + OUTER_RADIUS_SCALED) * 255) / (2 * OUTER_RADIUS_SCALED);
            if (yNormalized < 0) yNormalized = 0;
            if (yNormalized > 255) yNormalized = 255;

            // Map to band index
            uint8_t bandIndex = (yNormalized * NUM_BANDS) / 256;
            if (bandIndex >= NUM_BANDS) bandIndex = NUM_BANDS - 1;

            arm.pixels[p] = bandColors[bandIndex];
        }
    }
}
