#include "effects.h"
#include <FastLED.h>
#include <cmath>
#include "hardware_config.h"

// Colors
static CRGB OFF_COLOR = CRGB::Black;
static CRGB WHITE = CRGB::White;

/**
 * Render diagnostic test pattern - 20 discrete tests
 *
 * 360° divided into 20 patterns of 18° each:
 *
 * Patterns 0-3 (0-71°): Full RGB Combination Tests (all LEDs)
 * Patterns 4-7 (72-143°): Striped Alignment Tests (positions 0,4,9)
 * Patterns 8-11 (144-215°): Arm A (Inner) individual color tests
 * Patterns 12-15 (216-287°): Arm B (Middle) individual color tests
 * Patterns 16-19 (288-359°): Arm C (Outer) individual color tests
 */
void renderSolidArms(const RenderContext& ctx) {
    // Lambda to render one arm based on its current angle
    auto renderArm = [&](double angle, uint16_t armStart, uint8_t armIndex) {
        // Normalize angle to 0-359
        double normAngle = fmod(angle, 360.0);
        if (normAngle < 0) normAngle += 360.0;

        // Determine pattern (0-19)
        uint8_t pattern = (uint8_t)(normAngle / 18.0);
        if (pattern > 19) pattern = 19; // Safety clamp

        // Determine what this arm should show
        CRGB armColor = OFF_COLOR;
        bool fullLeds = true; // true = all LEDs, false = only 0,4,9

        if (pattern <= 3) {
            // ====== Patterns 0-3: Full RGB Combinations ======
            fullLeds = true;
            CRGB colors[4][3] = {
                // Pattern 0: A=red, B=green, C=blue
                {CRGB(255, 0, 0), CRGB(0, 255, 0), CRGB(0, 0, 255)},
                // Pattern 1: A=green, B=blue, C=red
                {CRGB(0, 255, 0), CRGB(0, 0, 255), CRGB(255, 0, 0)},
                // Pattern 2: A=blue, B=red, C=green
                {CRGB(0, 0, 255), CRGB(255, 0, 0), CRGB(0, 255, 0)},
                // Pattern 3: All white
                {WHITE, WHITE, WHITE}
            };
            armColor = colors[pattern][armIndex];
        }
        else if (pattern <= 7) {
            // ====== Patterns 4-7: Striped Alignment Tests ======
            fullLeds = false;
            CRGB colors[4][3] = {
                // Pattern 4: A=red, B=green, C=blue (striped)
                {CRGB(255, 0, 0), CRGB(0, 255, 0), CRGB(0, 0, 255)},
                // Pattern 5: A=green, B=blue, C=red (striped)
                {CRGB(0, 255, 0), CRGB(0, 0, 255), CRGB(255, 0, 0)},
                // Pattern 6: A=blue, B=red, C=green (striped)
                {CRGB(0, 0, 255), CRGB(255, 0, 0), CRGB(0, 255, 0)},
                // Pattern 7: All white (striped)
                {WHITE, WHITE, WHITE}
            };
            armColor = colors[pattern - 4][armIndex];
        }
        else if (pattern <= 11) {
            // ====== Patterns 8-11: Arm A (Inner) Individual Tests ======
            fullLeds = true;
            if (armIndex == 0) { // Arm A only
                CRGB colors[4] = {
                    CRGB(255, 0, 0),   // Pattern 8: red
                    CRGB(0, 255, 0),   // Pattern 9: green
                    CRGB(0, 0, 255),   // Pattern 10: blue
                    WHITE              // Pattern 11: white
                };
                armColor = colors[pattern - 8];
            }
            // Other arms stay off
        }
        else if (pattern <= 15) {
            // ====== Patterns 12-15: Arm B (Middle) Individual Tests ======
            fullLeds = true;
            if (armIndex == 1) { // Arm B only
                CRGB colors[4] = {
                    CRGB(255, 0, 0),   // Pattern 12: red
                    CRGB(0, 255, 0),   // Pattern 13: green
                    CRGB(0, 0, 255),   // Pattern 14: blue
                    WHITE              // Pattern 15: white
                };
                armColor = colors[pattern - 12];
            }
            // Other arms stay off
        }
        else {
            // ====== Patterns 16-19: Arm C (Outer) Individual Tests ======
            fullLeds = true;
            if (armIndex == 2) { // Arm C only
                CRGB colors[4] = {
                    CRGB(255, 0, 0),   // Pattern 16: red
                    CRGB(0, 255, 0),   // Pattern 17: green
                    CRGB(0, 0, 255),   // Pattern 18: blue
                    WHITE              // Pattern 19: white
                };
                armColor = colors[pattern - 16];
            }
            // Other arms stay off
        }

        // Render LEDs
        for (uint16_t ledIdx = 0; ledIdx < HardwareConfig::LEDS_PER_ARM; ledIdx++) {
            if (fullLeds) {
                // Light all LEDs
                ctx.leds[armStart + ledIdx] = armColor;
            } else {
                // Striped pattern - only positions 0, 4, 9
                if (ledIdx == 0 || ledIdx == 4 || ledIdx == 9) {
                    ctx.leds[armStart + ledIdx] = armColor;
                } else {
                    ctx.leds[armStart + ledIdx] = OFF_COLOR;
                }
            }
        }
    };

    // Render each arm based on its current angle
    // Arm A = Inner (index 0, LEDs 10-19)
    renderArm(ctx.innerArmDegrees, HardwareConfig::INNER_ARM_START, 0);
    // Arm B = Middle (index 1, LEDs 0-9)
    renderArm(ctx.middleArmDegrees, HardwareConfig::MIDDLE_ARM_START, 1);
    // Arm C = Outer (index 2, LEDs 20-29)
    renderArm(ctx.outerArmDegrees, HardwareConfig::OUTER_ARM_START, 2);
}
