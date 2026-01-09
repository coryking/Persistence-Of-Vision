#ifndef RENDER_CONTEXT_H
#define RENDER_CONTEXT_H

#include <FastLED.h>
#include "geometry.h"
#include "hardware_config.h"

/**
 * Render context passed to every effect
 *
 * Exposes the physical reality: 3 arms, each at its own angle,
 * each with 10 radial LEDs. Effects write to arms[].pixels[].
 *
 * Ownership: Context owns the pixel buffers. Effects write to them.
 * After render() returns, the caller copies to the actual LED strip.
 */
struct RenderContext {
    // === Timing ===
    uint32_t frameCount;          // Frame number (incremented every render)
    uint32_t timeUs;              // Current timestamp (microseconds)
    interval_t microsPerRev;      // Microseconds per revolution
    angle_t slotSizeUnits;        // Angular resolution (angle units per slot, 10 units = 1 degree)

    // === Convenience Methods ===

    // REMOVED: rpm() and degreesPerRender() methods
    // These used float division which is ~2x slower than integer math on ESP32-S3.
    // Effects should use microsPerRev directly with speedFactor8() helper instead.
    //
    // For debug display only, you can use:
    //   uint32_t rpm = 60000000UL / microsPerRev;
    //   (but keep this OUT of render path!)

    // === The Three Arms (physical reality) ===
    struct Arm {
        angle_t angleUnits;       // THIS arm's current angle (3600 = 360 degrees)
        CRGB pixels[HardwareConfig::LEDS_PER_ARM];  // THIS arm's LEDs: [0]=hub, [LEDS_PER_ARM-1]=tip
    } arms[3];                    // [0]=outer(+240°), [1]=middle(0°/hall), [2]=inside(+120°)

    // === Virtual Pixel Access ===
    //
    // Virtual pixels 0-32 map to physical LEDs in radial order:
    //   virt 0  = arm0:led0 (outermost - arm0 is outer)
    //   virt 1  = arm1:led0 (middle)
    //   virt 2  = arm2:led0 (innermost - arm2 is inside)
    //   virt 3  = arm0:led1
    //   ...
    //   virt 32 = arm2:led10 (innermost tip)
    //
    // Note: These 33 "virtual pixels" are at 3 different angular
    // positions right now! The virtual line only exists when spinning.

    /**
     * Access virtual pixel by position (0-32)
     */
    CRGB& virt(uint8_t v) {
        return arms[v % 3].pixels[v / 3];
    }

    const CRGB& virt(uint8_t v) const {
        return arms[v % 3].pixels[v / 3];
    }

    /**
     * Fill virtual pixel range with solid color
     *
     * @param start First virtual pixel (inclusive)
     * @param end Last virtual pixel (exclusive)
     * @param color Color to fill
     */
    void fillVirtual(uint8_t start, uint8_t end, CRGB color) {
        for (uint8_t v = start; v < end && v < 33; v++) {
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
        for (uint8_t v = start; v < end && v < 33; v++) {
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
