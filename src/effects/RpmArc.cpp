#include "effects.h"
#include <FastLED.h>
#include <cmath>
#include "types.h"
#include "hardware_config.h"
#include "arm_renderer.h"

// External references to globals from main.cpp
extern const uint8_t PHYSICAL_TO_VIRTUAL[30];

// RPM Arc Effect Configuration
constexpr float RPM_MIN = 800.0f;           // Minimum RPM (1 virtual pixel)
constexpr float RPM_MAX = 2500.0f;          // Maximum RPM (30 virtual pixels)
constexpr float ARC_WIDTH_DEGREES = 20.0f;  // Arc width in degrees
constexpr float ARC_CENTER_DEGREES = 0.0f;  // Arc center position (hall sensor)

// Colors
static const CRGB OFF_COLOR = CRGB::Black;

// Pre-computed gradient: green (innermost) → red (outermost)
static CRGB rpmGradient[30];
static bool gradientInitialized = false;

/**
 * Initialize static gradient table (called once)
 */
static void initializeRpmGradient() {
    if (gradientInitialized) return;

    for (uint8_t i = 0; i < 30; i++) {
        float t = static_cast<float>(i) / 29.0f;
        uint8_t hue = 85 * (1.0f - t);  // Green (85) → Red (0)
        rpmGradient[i] = CHSV(hue, 255, 255);
    }
    gradientInitialized = true;
}

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
void renderRpmArc(RenderContext& ctx) {
    // One-time gradient initialization
    initializeRpmGradient();

    // Calculate current RPM and map to pixel count
    float currentRPM = calculateRPM(ctx.microsecondsPerRev);
    uint8_t pixelCount = rpmToPixelCount(currentRPM);

    // Render all arms using helper
    renderAllArms(ctx, [&](uint16_t physicalLed, uint16_t ledIdx, const ArmInfo& arm) {
        // Check if this arm is in the 20-degree arc centered at 0°
        if (isAngleInRpmArc(arm.angle)) {
            // Light up pixels from innermost (virtual 0) to pixelCount-1
            uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];

            // Only light pixels within the RPM-based range
            if (virtualPos < pixelCount) {
                ctx.leds[physicalLed] = rpmGradient[virtualPos];
            } else {
                ctx.leds[physicalLed] = OFF_COLOR;
            }
        } else {
            // Outside arc - turn all LEDs off
            ctx.leds[physicalLed] = OFF_COLOR;
        }
    });
}
