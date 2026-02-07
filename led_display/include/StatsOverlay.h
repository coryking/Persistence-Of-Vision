#ifndef STATS_OVERLAY_H
#define STATS_OVERLAY_H

#include "RenderContext.h"
#include "RevolutionTimer.h"
#include "hardware_config.h"
#include "polar_helpers.h"
#include "geometry.h"
#include <FastLED.h>
#include <fl/five_bit_hd_gamma.h>
#include <NeoPixelBus.h>

/**
 * Stats Overlay - Diagnostic visualization system
 *
 * Draws radial bar indicators directly on the LED strip for real-time
 * performance monitoring:
 *
 * 1. Angular Resolution Bar (0°, width = 4× slot width)
 *    - Full height (rings 3-39)
 *    - Green (high res) → Red (low res)
 *
 * 2. Render Time Bar (~30°, width ~3°)
 *    - Height scaled at 10µs per ring (max 370µs)
 *    - Green gradient along bar
 *
 * 3. Output Time Bar (~45°, width ~3°)
 *    - Height scaled at 10µs per ring (max 370µs)
 *    - Blue gradient along bar
 *
 * Overlay occupies 0°-180°, rings 3-39. Effects may use rings 0-2 and
 * 180°-360° for their own debug visuals (via ctx.statsEnabled).
 *
 * Rendering: Runs in OutputTask AFTER brightness is applied, so stats
 * always render at full brightness. Writes directly to strip buffer using
 * HD gamma correction.
 */
class StatsOverlay {
public:
    /**
     * Render stats overlay onto the LED strip
     *
     * @param ctx Render context with arm angles
     * @param strip NeoPixelBus strip to write to
     * @param revTimer Revolution timer for timing data
     */
    template<typename T_STRIP>
    void render(const RenderContext& ctx, T_STRIP& strip, const RevolutionTimer& revTimer) {
        // Bar positions and dimensions (angle units, 10 = 1°)
        constexpr angle_t RESOLUTION_BAR_CENTER = 0;
        const angle_t RESOLUTION_BAR_WIDTH = ctx.angularSlotWidth * 4;

        constexpr angle_t RENDER_BAR_CENTER = 300;   // ~30°
        constexpr angle_t RENDER_BAR_WIDTH = 30;     // ~3°

        constexpr angle_t OUTPUT_BAR_CENTER = 450;   // ~45°
        constexpr angle_t OUTPUT_BAR_WIDTH = 30;     // ~3°

        // Timing data from revTimer (rolling averages, thread-safe)
        uint32_t avgRenderTimeUs = revTimer.getAverageRenderTime();
        uint32_t avgOutputTimeUs = revTimer.getAverageOutputTime();

        // Process each arm
        for (uint8_t a = 0; a < HardwareConfig::NUM_ARMS; a++) {
            angle_t armAngle = ctx.arms[a].angle;

            // Check which bars this arm intersects
            bool inResolutionBar = isAngleInArcUnits(armAngle, RESOLUTION_BAR_CENTER, RESOLUTION_BAR_WIDTH);
            bool inRenderBar = isAngleInArcUnits(armAngle, RENDER_BAR_CENTER, RENDER_BAR_WIDTH);
            bool inOutputBar = isAngleInArcUnits(armAngle, OUTPUT_BAR_CENTER, OUTPUT_BAR_WIDTH);

            if (!inResolutionBar && !inRenderBar && !inOutputBar) {
                continue;  // This arm doesn't intersect any bars
            }

            // Arm parameters for strip mapping
            uint16_t armStart = HardwareConfig::ARM_START[a];
            uint16_t armCount = HardwareConfig::ARM_LED_COUNT[a];
            bool armReversed = HardwareConfig::ARM_LED_REVERSED[a];

            // Start at LED 1 (ring 3/4/5 depending on arm), leaving rings 0-2 for effects
            uint8_t startLED = (a == 0) ? 1 : 0;  // arm[0] has extra LED at position 0

            // Blank the column (all LEDs on this arm, rings 3-39)
            for (uint8_t p = startLED; p < armCount; p++) {
                uint16_t stripPos = armStart + (armReversed ? (armCount - 1 - p) : p);
                strip.SetPixelColor(stripPos, RgbwColor(0, 0, 0, 0));
            }

            // Draw bars
            if (inResolutionBar) {
                drawResolutionBar(strip, a, armStart, armCount, armReversed, startLED, ctx.angularSlotWidth);
            }

            if (inRenderBar) {
                drawTimingBar(strip, a, armStart, armCount, armReversed, startLED, avgRenderTimeUs, true);
            }

            if (inOutputBar) {
                drawTimingBar(strip, a, armStart, armCount, armReversed, startLED, avgOutputTimeUs, false);
            }
        }
    }

private:
    /**
     * Draw angular resolution indicator bar
     * Full height, color mapped green (high res) → red (low res)
     */
    template<typename T_STRIP>
    void drawResolutionBar(T_STRIP& strip, uint8_t arm, uint16_t armStart, uint16_t armCount,
                          bool armReversed, uint8_t startLED, angle_t slotWidth) {
        // Map slot width to hue: 5 units (0.5°) = green, 200 units (20°) = red
        // Using CHSV: hue 96 = green, hue 0 = red
        constexpr angle_t MIN_SLOT = 5;    // 0.5° (highest res)
        constexpr angle_t MAX_SLOT = 200;  // 20° (lowest res)

        uint8_t hue;
        if (slotWidth <= MIN_SLOT) {
            hue = 96;  // Green
        } else if (slotWidth >= MAX_SLOT) {
            hue = 0;   // Red
        } else {
            // Linear interpolation from green (96) to red (0)
            // As slotWidth increases, hue decreases
            uint16_t range = MAX_SLOT - MIN_SLOT;
            uint16_t position = slotWidth - MIN_SLOT;
            hue = 96 - static_cast<uint8_t>((96 * position) / range);
        }

        CHSV color(hue, 255, 255);
        CRGB rgb;
        hsv2rgb_rainbow(color, rgb);

        // Draw full height bar
        for (uint8_t p = startLED; p < armCount; p++) {
            uint16_t stripPos = armStart + (armReversed ? (armCount - 1 - p) : p);
            writePixelWithGamma(strip, stripPos, rgb);
        }
    }

    /**
     * Draw timing bar (render or output time)
     * Height scaled at 10µs per ring, gradient along bar
     */
    template<typename T_STRIP>
    void drawTimingBar(T_STRIP& strip, uint8_t arm, uint16_t armStart, uint16_t armCount,
                      bool armReversed, uint8_t startLED, uint32_t timeUs, bool isRenderBar) {
        // Scale: 10µs per ring, max 37 rings = 370µs
        constexpr uint32_t US_PER_RING = 10;
        uint32_t maxRings = armCount - startLED;  // Available rings (3-39)
        uint32_t barRings = (timeUs / US_PER_RING);
        if (barRings > maxRings) barRings = maxRings;

        if (barRings == 0) return;  // Nothing to draw

        // Color gradients
        // Render bar: green family (hue 96 at base → 128 at tip)
        // Output bar: cyan/blue family (hue 160 at base → 192 at tip)
        uint8_t baseHue = isRenderBar ? 96 : 160;
        uint8_t tipHue = isRenderBar ? 128 : 192;

        // Draw bar from base (startLED) to tip (startLED + barRings - 1)
        for (uint32_t i = 0; i < barRings; i++) {
            uint8_t p = startLED + i;
            if (p >= armCount) break;

            // Gradient along bar
            uint8_t hue = map(i, 0, barRings - 1, baseHue, tipHue);
            CHSV color(hue, 255, 255);
            CRGB rgb;
            hsv2rgb_rainbow(color, rgb);

            uint16_t stripPos = armStart + (armReversed ? (armCount - 1 - p) : p);
            writePixelWithGamma(strip, stripPos, rgb);
        }
    }

    /**
     * Write pixel to strip with HD gamma correction at full brightness
     */
    template<typename T_STRIP>
    void writePixelWithGamma(T_STRIP& strip, uint16_t stripPos, const CRGB& color) {
        CRGB output;
        uint8_t brightness5bit;
        fl::five_bit_hd_gamma_bitshift(color, CRGB(255, 255, 255), 255, &output, &brightness5bit);
        strip.SetPixelColor(stripPos, RgbwColor(output.r, output.g, output.b, brightness5bit));
    }
};

#endif // STATS_OVERLAY_H
