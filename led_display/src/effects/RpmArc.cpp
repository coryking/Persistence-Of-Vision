#include "effects/RpmArc.h"
#include "polar_helpers.h"

void RpmArc::begin() {
    initializeGradient();
    arcWidthUnits = BASE_ARC_WIDTH_UNITS;
}

void RpmArc::initializeGradient() {
    // Green (innermost) → Red (outermost)
    for (uint8_t i = 0; i < 30; i++) {
        float t = static_cast<float>(i) / 29.0f;
        uint8_t hue = static_cast<uint8_t>(85.0f * (1.0f - t));  // Green (85) → Red (0)
        gradient[i] = CHSV(hue, 255, 255);
    }
}

uint8_t RpmArc::speedToPixelCount(uint8_t speedFactor) const {
    // speedFactor is 0-255 (faster = higher)
    // Map to 1-30 pixels (always show at least 1 pixel)
    return 1 + scale8(29, speedFactor);
}

/**
 * Render RPM-based growing arc effect
 *
 * Each arm is checked independently against the arc, allowing
 * partial visibility when arms straddle the arc boundary.
 */
void RpmArc::render(RenderContext& ctx) {
    ctx.clear();

    // Calculate speed-based parameters
    uint8_t speed = speedFactor8(ctx.revolutionPeriodUs);
    uint8_t pixelCount = speedToPixelCount(speed);

    // Animate arc width based on speed (wider at higher speed)
    arcWidthUnits = BASE_ARC_WIDTH_UNITS + scale8(MAX_EXTRA_WIDTH_UNITS, speed);

    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        // Get intensity for this arm (0-255: 0 = outside arc, 255 = at center)
        uint8_t intensity = arcIntensityUnits(arm.angle, ARC_CENTER_UNITS, arcWidthUnits);

        if (intensity == 0) {
            // Arm completely outside arc - already cleared
            continue;
        }

        // Fill radial pixels up to RPM-based limit
        for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++) {
            uint8_t virtualPos = a + p * 3;

            if (virtualPos < pixelCount) {
                CRGB color = gradient[virtualPos];
                // Apply arc edge fade (intensity already 0-255)
                color.nscale8(intensity);
                arm.pixels[p] = color;
            }
            // Pixels beyond pixelCount stay black (from clear)
        }
    }
}
