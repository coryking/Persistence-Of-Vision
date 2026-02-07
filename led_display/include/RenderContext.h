#ifndef RENDER_CONTEXT_H
#define RENDER_CONTEXT_H

#include <FastLED.h>
#include "geometry.h"
#include "hardware_config.h"

/**
 * Render context passed to every effect
 *
 * Exposes the physical reality: 3 arms, each at its own angle,
 * each with up to 14 radial LEDs. Effects write to arms[].pixels[].
 * arm[0] (ARM3/outer) has 14 LEDs, arm[1] and arm[2] have 13 LEDs each.
 *
 * Ownership: Context owns the pixel buffers. Effects write to them.
 * After render() returns, the caller copies to the actual LED strip.
 */
struct RenderContext {
    // === Timing ===
    uint32_t  frameNumber;        // Sequential frame counter
    uint32_t  timestampUs;        // This frame's wall-clock timestamp in µs
    uint32_t  frameDeltaUs;       // Microseconds since previous render (0 on first frame)
    period_t  revolutionPeriodUs; // Duration of last revolution in µs
    angle_t   angularSlotWidth;   // Angular resolution per render slot (angle units, 10 = 1°)

    // === Convenience ===

    /** Normalized spin speed (0 = stopped/slow, 255 = max motor speed) */
    uint8_t spinSpeed() const {
        if (revolutionPeriodUs >= MICROS_PER_REV_MAX) return 0;
        if (revolutionPeriodUs <= MICROS_PER_REV_MIN) return 255;
        return static_cast<uint8_t>(
            (MICROS_PER_REV_MAX - revolutionPeriodUs) * 255 /
            (MICROS_PER_REV_MAX - MICROS_PER_REV_MIN)
        );
    }

    // === The Three Arms (physical reality) ===
    struct Arm {
        angle_t angle;            // THIS arm's angular position (3600 = 360°)
        CRGB pixels[HardwareConfig::LEDS_PER_ARM];  // THIS arm's LEDs: [0]=hub, [LEDS_PER_ARM-1]=tip
    } arms[3];                    // [0]=outer/ARM3(+240°,14LEDs), [1]=middle/ARM2(0°/hall,13LEDs), [2]=inside/ARM1(+120°,13LEDs)

    // === Virtual Pixel Access ===
    //
    // Virtual pixels 0-39 map to 40 physical LEDs in radial order.
    // ARM3 (arm[0]) has 14 LEDs while ARM1/ARM2 have 13 each.
    // ARM3's extra LED is at the hub (innermost position).
    //
    // Mapping:
    //   virt 0  = arm[0]:led0 (ARM3's extra inner LED - no matching LED on other arms)
    //   virt 1  = arm[0]:led1, virt 2 = arm[1]:led0, virt 3 = arm[2]:led0 (radial row 1)
    //   virt 4  = arm[0]:led2, virt 5 = arm[1]:led1, virt 6 = arm[2]:led1 (radial row 2)
    //   ...
    //   virt 37 = arm[0]:led13, virt 38 = arm[1]:led12, virt 39 = arm[2]:led12 (radial row 13/tip)
    //
    // Note: These 40 "virtual pixels" are at 3 different angular
    // positions right now! The virtual line only exists when spinning.

private:
    // Lookup tables for 40-LED virtual pixel mapping
    // ARM3 (arm[0]) has 14 LEDs, ARM1/ARM2 have 13 each
    static constexpr uint8_t VIRT_ARM[40] = {
        0,          // v=0: ARM3's extra inner
        0, 1, 2,    // v=1-3: radial row 1
        0, 1, 2,    // v=4-6: radial row 2
        0, 1, 2,    // v=7-9: radial row 3
        0, 1, 2,    // v=10-12: radial row 4
        0, 1, 2,    // v=13-15: radial row 5
        0, 1, 2,    // v=16-18: radial row 6
        0, 1, 2,    // v=19-21: radial row 7
        0, 1, 2,    // v=22-24: radial row 8
        0, 1, 2,    // v=25-27: radial row 9
        0, 1, 2,    // v=28-30: radial row 10
        0, 1, 2,    // v=31-33: radial row 11
        0, 1, 2,    // v=34-36: radial row 12
        0, 1, 2     // v=37-39: radial row 13
    };
    static constexpr uint8_t VIRT_PIXEL[40] = {
        0,              // v=0: ARM3's extra inner
        1, 0, 0,        // v=1-3: radial row 1
        2, 1, 1,        // v=4-6: radial row 2
        3, 2, 2,        // v=7-9: radial row 3
        4, 3, 3,        // v=10-12: radial row 4
        5, 4, 4,        // v=13-15: radial row 5
        6, 5, 5,        // v=16-18: radial row 6
        7, 6, 6,        // v=19-21: radial row 7
        8, 7, 7,        // v=22-24: radial row 8
        9, 8, 8,        // v=25-27: radial row 9
        10, 9, 9,       // v=28-30: radial row 10
        11, 10, 10,     // v=31-33: radial row 11
        12, 11, 11,     // v=34-36: radial row 12
        13, 12, 12      // v=37-39: radial row 13
    };

public:
    /**
     * Access virtual pixel by position (0-39)
     */
    CRGB& virt(uint8_t v) {
        return arms[VIRT_ARM[v]].pixels[VIRT_PIXEL[v]];
    }

    const CRGB& virt(uint8_t v) const {
        return arms[VIRT_ARM[v]].pixels[VIRT_PIXEL[v]];
    }

    /**
     * Fill virtual pixel range with solid color
     *
     * @param start First virtual pixel (inclusive)
     * @param end Last virtual pixel (exclusive)
     * @param color Color to fill
     */
    void fillVirtual(uint8_t start, uint8_t end, CRGB color) {
        for (uint8_t v = start; v < end && v < HardwareConfig::TOTAL_LOGICAL_LEDS; v++) {
            virt(v) = color;
        }
    }

    /**
     * Fill virtual pixel range with gradient from palette
     *
     * @param start First virtual pixel (inclusive)
     * @param end Last virtual pixel (exclusive)
     * @param palette Color palette to sample
     * @param paletteStart Palette index for start pixel (0-255)
     * @param paletteEnd Palette index for end pixel (0-255)
     */
    void fillVirtualGradient(uint8_t start, uint8_t end,
                             const CRGBPalette16& palette,
                             uint8_t paletteStart = 0,
                             uint8_t paletteEnd = 255) {
        if (end <= start) return;
        for (uint8_t v = start; v < end && v < HardwareConfig::TOTAL_LOGICAL_LEDS; v++) {
            uint8_t palIdx = map(v - start, 0, end - start - 1, paletteStart, paletteEnd);
            virt(v) = ColorFromPalette(palette, palIdx);
        }
    }

    /**
     * Clear all pixels to black
     */
    void clear() {
        for (auto& arm : arms) {
            fill_solid(arm.pixels, HardwareConfig::LEDS_PER_ARM, CRGB::Black);
        }
    }
};

#endif // RENDER_CONTEXT_H
