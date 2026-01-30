#include "effects/Radar.h"
#include <FastLED.h>
#include "polar_helpers.h"

void Radar::begin() {
    sweepAngleUnits = 0;
    Serial.println("[Radar] Effect started");
}

bool Radar::isRangeRing(uint8_t vPixel) const {
    // Range rings at 1/4, 1/2, 3/4 of total virtual pixels
    uint8_t ring1 = HardwareConfig::TOTAL_LOGICAL_LEDS / 4;
    uint8_t ring2 = HardwareConfig::TOTAL_LOGICAL_LEDS / 2;
    uint8_t ring3 = (HardwareConfig::TOTAL_LOGICAL_LEDS * 3) / 4;

    return (vPixel == ring1 || vPixel == ring2 || vPixel == ring3);
}

void IRAM_ATTR Radar::render(RenderContext& ctx) {
    // Render each arm
    for (int armIdx = 0; armIdx < HardwareConfig::NUM_ARMS; armIdx++) {
        auto& arm = ctx.arms[armIdx];
        angle_t armAngle = arm.angleUnits;

        for (int led = 0; led < HardwareConfig::ARM_LED_COUNT[armIdx]; led++) {
            uint8_t vPixel = armLedToVirtual(armIdx, led);

            // Start with black
            CRGB color = CRGB::Black;

            // === Layer 1: Range rings (dimmest, rendered first) ===
            if (isRangeRing(vPixel)) {
                color = RANGE_RING_COLOR;
            }

            // === Layer 2: Phosphor trail (additive blend) ===
            // Trail is BEHIND the sweep (in the direction opposite to sweep rotation)
            int16_t trailDist = angularDistanceUnits(sweepAngleUnits, armAngle);

            // Trail is behind sweep, so we want negative distances (arm behind sweep)
            // Convert to positive "distance behind sweep"
            int16_t distBehind = -trailDist;
            if (distBehind < 0) {
                distBehind += ANGLE_FULL_CIRCLE;
            }

            if (distBehind > 0 && distBehind <= static_cast<int16_t>(TRAIL_WIDTH)) {
                // Exponential decay: intensity = (1 - dist/width)^2
                uint32_t ratio = (TRAIL_WIDTH - distBehind) * 256 / TRAIL_WIDTH;
                uint8_t intensity = static_cast<uint8_t>((ratio * ratio) >> 8);

                // Blend between dim and bright phosphor
                CRGB phosphor = blend(PHOSPHOR_DIM, PHOSPHOR_BRIGHT, intensity);
                phosphor.nscale8(intensity);
                color += phosphor;
            }

            // === Layer 3: Sweep beam (brightest, on top) ===
            angle_t sweepDist = angularDistanceAbsUnits(armAngle, sweepAngleUnits);
            if (sweepDist <= ctx.slotSizeUnits) {
                // Sweep beam is exactly 1 slot wide (thinnest possible)
                color = SWEEP_COLOR;
            }

            arm.pixels[led] = color;
        }
    }
}

void Radar::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    (void)usPerRev;
    (void)timestamp;
    (void)revolutionCount;

    // Advance sweep angle (revolution-count based to avoid wall-clock desync)
    sweepAngleUnits = (sweepAngleUnits + SWEEP_ANGLE_PER_REV) % ANGLE_FULL_CIRCLE;
}

void Radar::nextMode() {
    // No modes - sweep speed is fixed
}

void Radar::prevMode() {
    // No modes - sweep speed is fixed
}

void Radar::paramUp() {
    // No parameters
}

void Radar::paramDown() {
    // No parameters
}
