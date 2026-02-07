#include "effects/ProjectionTest.h"
#include "hardware_config.h"
#include "geometry.h"
#include <FastLED.h>  // for sin8

// Jupiter palette: high contrast bands with varying visual weight
// Arranged to create thick/thin appearance through color contrast
static const CRGB bandColors[] = {
    CRGB(240, 220, 190),  // bright cream (wide-feeling)
    CRGB(180, 100, 60),   // rust orange
    CRGB(255, 240, 220),  // off-white (wide-feeling)
    CRGB(120, 70, 40),    // dark brown (thin accent)
    CRGB(230, 180, 140),  // peachy tan
    CRGB(200, 80, 50),    // burnt orange (Great Red Spot vibe)
    CRGB(250, 235, 200),  // pale cream
    CRGB(90, 50, 30),     // deep brown (thin accent)
};
static constexpr uint8_t NUM_BANDS = sizeof(bandColors) / sizeof(bandColors[0]);

// Physical radius constants derived from RadialGeometry
// Using 8-bit fixed point: actual_mm = value / 2
static constexpr uint8_t INNER_RADIUS_SCALED = static_cast<uint8_t>(RadialGeometry::INNERMOST_LED_CENTER_MM * 2);
static constexpr uint8_t OUTER_RADIUS_SCALED = static_cast<uint8_t>(RadialGeometry::OUTERMOST_LED_CENTER_MM * 2);
static constexpr uint8_t RADIUS_SPAN_SCALED = OUTER_RADIUS_SCALED - INNER_RADIUS_SCALED;

void ProjectionTest::render(RenderContext& ctx) {
    // Rotate ~36°/second (full rotation in 10 sec)
    // 360° in 10 sec = 3600 units in 10,000,000 us
    rotationOffset += (ctx.frameDeltaUs * 36) / 100000;
    rotationOffset %= ANGLE_FULL_CIRCLE;

    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        // Effective angle for projection (disc angle + rotation offset)
        uint16_t effectiveAngle = (arm.angle + rotationOffset) % ANGLE_FULL_CIRCLE;

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

            // Map to band with edge-only anti-aliasing
            // bandPosition is 0 to (256 * NUM_BANDS - 1), giving sub-band precision
            uint16_t bandPosition = yNormalized * NUM_BANDS;
            uint8_t bandIndex = bandPosition / 256;
            uint8_t frac = bandPosition & 0xFF;  // position within band (0-255)

            if (bandIndex >= NUM_BANDS) bandIndex = NUM_BANDS - 1;

            // Anti-alias only at the END of each band (fading toward next)
            // Start-of-band blending is redundant and causes "ringing"
            constexpr uint8_t EDGE_WIDTH = 32;  // ~12% of band width

            CRGB color = bandColors[bandIndex];

            if (frac > (255 - EDGE_WIDTH) && bandIndex < NUM_BANDS - 1) {
                // Near end of band - fade toward next band
                uint8_t blendAmt = ((frac - (255 - EDGE_WIDTH)) * 255) / EDGE_WIDTH;
                color = blend(bandColors[bandIndex], bandColors[bandIndex + 1], blendAmt);
            }

            arm.pixels[p] = color;
        }
    }
}
