#include "effects.h"
#include <NeoPixelBus.h>
#include <NeoPixelBusLg.h>
#include <cmath>
#include "types.h"

// External references to globals from main.cpp
extern NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip;
extern const uint8_t PHYSICAL_TO_VIRTUAL[30];

// Hardware configuration (from main.cpp)
constexpr uint16_t LEDS_PER_ARM = 10;
constexpr uint16_t INNER_ARM_START = 10;
constexpr uint16_t MIDDLE_ARM_START = 0;
constexpr uint16_t OUTER_ARM_START = 20;

// RPM Arc Effect Configuration
constexpr float RPM_MIN = 800.0f;           // Minimum RPM (1 virtual pixel)
constexpr float RPM_MAX = 2500.0f;          // Maximum RPM (30 virtual pixels)
constexpr float ARC_WIDTH_DEGREES = 20.0f;  // Arc width in degrees
constexpr float ARC_CENTER_DEGREES = 0.0f;  // Arc center position (hall sensor)

// Colors
static const RgbColor OFF_COLOR(0, 0, 0);

/**
 * Calculate RPM from microseconds per revolution
 */
static float calculateRPM(interval_t microsecondsPerRev) {
    if (microsecondsPerRev == 0) return 0.0f;
    return 60000000.0f / static_cast<float>(microsecondsPerRev);
}

/**
 * Map RPM to number of virtual pixels (1-30)
 */
static uint8_t rpmToPixelCount(float rpm) {
    // Clamp RPM to valid range
    if (rpm < RPM_MIN) rpm = RPM_MIN;
    if (rpm > RPM_MAX) rpm = RPM_MAX;

    // Linear mapping: 800 RPM = 1 pixel, 2500 RPM = 30 pixels
    float normalized = (rpm - RPM_MIN) / (RPM_MAX - RPM_MIN);
    uint8_t pixels = static_cast<uint8_t>(1.0f + normalized * 29.0f);

    // Ensure we're in valid range
    if (pixels < 1) pixels = 1;
    if (pixels > 30) pixels = 30;

    return pixels;
}

/**
 * Get color for a virtual pixel based on radial position (green to red gradient)
 * virtualPos: 0-29 (0 = innermost/green, 29 = outermost/red)
 */
static RgbColor getGradientColor(uint8_t virtualPos) {
    // Normalize position to 0.0-1.0
    float t = static_cast<float>(virtualPos) / 29.0f;

    // HSV: Green (H=120°) to Red (H=0°)
    // We go from 120° to 0°, which means 120 * (1-t)
    float hue = 120.0f * (1.0f - t) / 360.0f;  // Convert to 0.0-1.0 range

    HslColor hsl(hue, 1.0f, 0.5f);
    return RgbColor(hsl);
}

/**
 * Check if angle is within the arc (handles 360° wraparound)
 * Arc is centered at ARC_CENTER_DEGREES with width ARC_WIDTH_DEGREES
 */
static bool isAngleInRpmArc(double angle) {
    double halfWidth = ARC_WIDTH_DEGREES / 2.0;
    double arcStart = ARC_CENTER_DEGREES - halfWidth;
    double arcEnd = ARC_CENTER_DEGREES + halfWidth;

    // Normalize angle to 0-360
    angle = fmod(angle, 360.0);
    if (angle < 0) angle += 360.0;

    // Handle wraparound
    if (arcStart < 0) {
        // Arc wraps around 0 (e.g., 350° to 10°)
        return (angle >= (arcStart + 360.0)) || (angle < arcEnd);
    } else if (arcEnd > 360.0) {
        // Arc wraps around 360
        return (angle >= arcStart) || (angle < (arcEnd - 360.0));
    } else {
        // No wraparound
        return (angle >= arcStart) && (angle < arcEnd);
    }
}

/**
 * Render RPM-based growing arc effect
 */
void renderRpmArc(const RenderContext& ctx) {
    // Calculate current RPM and map to pixel count
    float currentRPM = calculateRPM(ctx.microsecondsPerRev);
    uint8_t pixelCount = rpmToPixelCount(currentRPM);

    // Render each arm independently
    auto renderRpmArm = [&](double angle, uint16_t armStart) {
        // Check if this arm is in the 20-degree arc centered at 0°
        if (isAngleInRpmArc(angle)) {
            // Light up pixels from innermost (virtual 0) to pixelCount-1
            for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
                uint16_t physicalLed = armStart + ledIdx;
                uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];

                // Only light pixels within the RPM-based range
                if (virtualPos < pixelCount) {
                    RgbColor color = getGradientColor(virtualPos);
                    strip.SetPixelColor(physicalLed, color);
                } else {
                    strip.SetPixelColor(physicalLed, OFF_COLOR);
                }
            }
        } else {
            // Outside arc - turn all LEDs off
            for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
                strip.SetPixelColor(armStart + ledIdx, OFF_COLOR);
            }
        }
    };

    // Render all three arms
    renderRpmArm(ctx.innerArmDegrees, INNER_ARM_START);
    renderRpmArm(ctx.middleArmDegrees, MIDDLE_ARM_START);
    renderRpmArm(ctx.outerArmDegrees, OUTER_ARM_START);
}
