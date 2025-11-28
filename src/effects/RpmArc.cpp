#include "effects/RpmArc.h"
#include "polar_helpers.h"

void RpmArc::begin() {
    initializeGradient();
    arcWidth = BASE_ARC_WIDTH;
}

void RpmArc::initializeGradient() {
    // Green (innermost) → Red (outermost)
    for (uint8_t i = 0; i < 30; i++) {
        float t = static_cast<float>(i) / 29.0f;
        uint8_t hue = static_cast<uint8_t>(85.0f * (1.0f - t));  // Green (85) → Red (0)
        gradient[i] = CHSV(hue, 255, 255);
    }
}

uint8_t RpmArc::rpmToPixelCount(float rpm) const {
    float clamped = constrain(rpm, RPM_MIN, RPM_MAX);
    float normalized = (clamped - RPM_MIN) / (RPM_MAX - RPM_MIN);
    return 1 + static_cast<uint8_t>(normalized * 29.0f);
}

/**
 * Render RPM-based growing arc effect
 *
 * Each arm is checked independently against the arc, allowing
 * partial visibility when arms straddle the arc boundary.
 */
void RpmArc::render(RenderContext& ctx) {
    ctx.clear();

    // Calculate RPM-based parameters
    float rpm = ctx.rpm();
    uint8_t pixelCount = rpmToPixelCount(rpm);

    // Optional: animate arc width based on RPM (wider at higher RPM)
    // Uncomment to enable: arcWidth = BASE_ARC_WIDTH + 10.0f * (rpm / RPM_MAX);

    for (int a = 0; a < 3; a++) {
        auto& arm = ctx.arms[a];

        // Get intensity for this arm (0 = outside arc, 1 = at center)
        float intensity = arcIntensity(arm.angle, ARC_CENTER, arcWidth);

        if (intensity == 0.0f) {
            // Arm completely outside arc - already cleared
            continue;
        }

        // Fill radial pixels up to RPM-based limit
        for (int p = 0; p < 10; p++) {
            uint8_t virtualPos = a + p * 3;

            if (virtualPos < pixelCount) {
                CRGB color = gradient[virtualPos];
                // Apply arc edge fade
                color.nscale8(static_cast<uint8_t>(intensity * 255));
                arm.pixels[p] = color;
            }
            // Pixels beyond pixelCount stay black (from clear)
        }
    }
}
