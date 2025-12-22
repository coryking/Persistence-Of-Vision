#include "effects/ArmAlignment.h"
#include "polar_helpers.h"
#include "hardware_config.h"
#include <algorithm>

void ArmAlignment::begin() {
    // Initialize state machine
    phaseStartTime = 0;
    currentPhase = 0;
    inFadeTransition = false;
    fadeLevel = 255;
    walkingPixelPosition = 0;
    lastSeenRevolution = 0;
}

void ArmAlignment::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    // Initialize timing on first call
    if (phaseStartTime == 0) {
        phaseStartTime = timestamp;
    }

    uint64_t elapsedUs = timestamp - phaseStartTime;
    uint64_t elapsedMs = elapsedUs / 1000;  // Integer division, no floats

    // State machine: arm display phases (0-2)
    if (currentPhase <= 2) {
        if (elapsedMs >= 5000 && !inFadeTransition) {
            // Start fade-to-black after 5 seconds
            inFadeTransition = true;
        }

        if (elapsedMs >= 5500) {
            // Fade complete, advance to next phase
            currentPhase++;
            phaseStartTime = timestamp;
            inFadeTransition = false;
            fadeLevel = 255;
        } else if (inFadeTransition) {
            // Calculate fade progress (5000-5500ms → 0-500ms)
            uint32_t fadeMs = elapsedMs - 5000;
            // fadeProgress: 0→255 over 500ms
            uint16_t fadeProgress = (fadeMs * 255) / 500;
            fadeLevel = 255 - std::min((uint16_t)255, fadeProgress);
        } else {
            fadeLevel = 255;  // Fully visible
        }
    }

    // Walking pixel phase (3+)
    if (currentPhase >= 3) {
        // Advance one position per revolution
        if (revolutionCount != lastSeenRevolution) {
            // Total virtual positions: 3 arms × 11 LEDs = 33 (0-32)
            walkingPixelPosition = (walkingPixelPosition + 1) % (HardwareConfig::NUM_ARMS * HardwareConfig::LEDS_PER_ARM);
            lastSeenRevolution = revolutionCount;

            // If completed full walk (wrapped back to 0), restart test sequence
            if (walkingPixelPosition == 0) {
                currentPhase = 0;
                phaseStartTime = timestamp;
                inFadeTransition = false;
                fadeLevel = 255;
            }
        }
    }
}

void ArmAlignment::render(RenderContext& ctx) {
    if (currentPhase <= 2) {
        renderArmOnly(ctx, currentPhase);
    } else {
        renderWalkingPixel(ctx);
    }
}

void ArmAlignment::renderArmOnly(RenderContext& ctx, uint8_t armIndex) {
    // Arm colors: R, G, B
    static const CRGB ARM_COLORS[3] = {
        CRGB(255, 0, 0),   // arm 0 = RED
        CRGB(0, 255, 0),   // arm 1 = GREEN
        CRGB(0, 0, 255)    // arm 2 = BLUE
    };

    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        if (a == armIndex) {
            // Active arm
            angle_t armAngle = arm.angleUnits;

            // Check if at 0° marker (±3°, same as SolidArms reference)
            if (armAngle < 30 || armAngle > 3570) {
                // White/orange reference marker for crisp visual
                CRGB color = (armAngle < 30) ? CRGB::White : CRGB::Orange;
                color.nscale8(fadeLevel);
                fill_solid(arm.pixels, HardwareConfig::LEDS_PER_ARM, color);
            } else {
                // Colored arc: fade from 255 at 0° to 0 at ±120°
                // Arc width: 240° (from -120° to +120°)
                // Opposite side (±120° to 180°) stays BLACK
                bool inColoredArc = isAngleInArcUnits(
                    armAngle,
                    ANGLE_UNITS(0),    // Center at hall position
                    ANGLE_UNITS(240)   // Arc width ±120° from center
                );

                if (inColoredArc) {
                    CRGB color = ARM_COLORS[a];
                    uint8_t brightness = arcIntensityUnits(
                        armAngle,
                        ANGLE_UNITS(0),
                        ANGLE_UNITS(240)
                    );
                    color.nscale8(brightness);
                    color.nscale8(fadeLevel);
                    fill_solid(arm.pixels, HardwareConfig::LEDS_PER_ARM, color);
                } else {
                    // Black (opposite side, from ±120° to 180°)
                    fill_solid(arm.pixels, HardwareConfig::LEDS_PER_ARM, CRGB::Black);
                }
            }
        } else {
            // Inactive arms: black
            fill_solid(arm.pixels, HardwareConfig::LEDS_PER_ARM, CRGB::Black);
        }
    }
}

void ArmAlignment::renderWalkingPixel(RenderContext& ctx) {
    // Clear all pixels
    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        fill_solid(ctx.arms[a].pixels, HardwareConfig::LEDS_PER_ARM, CRGB::Black);
    }

    // Map virtual position to arm index and LED position (INTERLEAVED)
    // Pattern: [a2p0, a1p0, a0p0, a2p1, a1p1, a0p1, a2p2, a1p2, a0p2, ...]
    // Position 0: arm[2] LED 0 (inside, innermost)
    // Position 1: arm[1] LED 0 (middle, innermost)
    // Position 2: arm[0] LED 0 (outer, innermost)
    // Position 3: arm[2] LED 1 (inside, second)
    // ... and so on
    uint8_t radialDistance = walkingPixelPosition / 3;  // Which LED position (0-10)
    uint8_t armOffset = walkingPixelPosition % 3;       // Which arm in this radial band (0-2)

    // Arm mapping: offset 0 = arm[2] (inside), offset 1 = arm[1] (middle), offset 2 = arm[0] (outer)
    static const uint8_t ARM_INTERLEAVE[3] = {2, 1, 0};
    uint8_t armIdx = ARM_INTERLEAVE[armOffset];
    uint8_t ledPos = radialDistance;

    // Color matches arm (same as arm display test)
    static const CRGB ARM_COLORS[3] = {
        CRGB(255, 0, 0),   // arm 0 = RED
        CRGB(0, 255, 0),   // arm 1 = GREEN
        CRGB(0, 0, 255)    // arm 2 = BLUE
    };

    ctx.arms[armIdx].pixels[ledPos] = ARM_COLORS[armIdx];
}
